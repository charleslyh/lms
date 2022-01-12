#include "Player.h"
#include "MediaSource.h"
#include "Decoder.h"
#include "Buffer.h"
#include "Logger.h"
#include "Runtime.h"
#include "Cell.h"
#include "Events.h"
extern "C" {
  #include <libavcodec/avcodec.h>
  #include "libavutil/avutil.h"
  #include <libavutil/opt.h>
  #include <libavformat/avformat.h>
  #include <libswscale/swscale.h>
  #include <libswresample/swresample.h>
  #include <libavutil/imgutils.h>
  #include <SDL2/SDL.h>
}
#include <vector>
#include <algorithm>

namespace lms {

constexpr double InvalidPlayingTime = -1.0;

class TimeSync : virtual public Object {
public:
  TimeSync() {
    timePivot  = 0;
    tickPivot = 0;
  }
  
  double getPlayingTime() const {
    uint32_t ticksPassed = SDL_GetTicks() - tickPivot;
    double secondsPassed = (double)ticksPassed / 1000.0;
    return timePivot + secondsPassed;
  }
  
  void updateTimePivot(double time) {
    LMSLogVerbose("time=%lf", time);
    timePivot  = time;
    tickPivot = SDL_GetTicks();
  }
  
private:
  std::atomic<double>   timePivot;
  std::atomic<uint64_t> tickPivot;
};

class RenderDriver : public Cell {
public:
  virtual void start() = 0;
  virtual void stop() = 0;
  
  virtual double cachedPlayingTime() = 0;
};

class SDLAudioResampler: public Cell {
  AVStream *stream;
  SwrContext *context;
  int out_channel_layout;
  int out_sample_rate;
  AVSampleFormat out_sample_format;

public:
  SDLAudioResampler(AVStream *stream) {
    this->stream = stream;
    this->out_sample_format  = AV_SAMPLE_FMT_S16;
    this->out_sample_rate    = stream->codecpar->sample_rate;
    this->out_channel_layout = stream->codecpar->channel_layout;

    this->context = swr_alloc();
    int64_t in_channel_layout  = stream->codecpar->channel_layout;
    int in_nb_channels = stream->codecpar->channels;
    int in_nb_samples = 0;
       
    // get input audio channels
    bool channels_matches_layout = (in_nb_channels == av_get_channel_layout_nb_channels(in_channel_layout));
    if (!channels_matches_layout) {
      in_channel_layout = av_get_default_channel_layout(in_nb_channels);
    }
    
    int ret = 0;
    setOption("in_channel_layout",  in_channel_layout);
    setOption("in_sample_rate",     stream->codecpar->sample_rate);
    setOption("in_sample_fmt",      stream->codecpar->format);
    setOption("out_channel_layout", out_channel_layout);
    setOption("out_sample_rate",    out_sample_rate);
    setOption("out_sample_fmt",     out_sample_format);

    ret = swr_init(context);
  }

  ~SDLAudioResampler() {
    swr_free(&this->context);
  }
  
  inline void setOption(const char *name, int value) {
    av_opt_set_int(context, name, value, 0);
  }

public:
  void start() override {}
  void stop() override {}
  
  void didReceivePipelineMessage(const PipelineMessage& msg) override {
    auto avfrm = (AVFrame *)msg.at("frame").value.ptr;
    int out_linesize = 0;
    uint8_t **resampled_data = NULL;
    int resampled_data_size = 0;
    int out_nb_channels = av_get_channel_layout_nb_channels(out_channel_layout);

    int max_out_nb_samples = av_rescale_rnd(avfrm->nb_samples,
                                            out_sample_rate,
                                            avfrm->sample_rate,
                                            AV_ROUND_UP);
    
    int ret = av_samples_alloc_array_and_samples(&resampled_data,
                                                 &out_linesize,
                                                 out_nb_channels,
                                                 max_out_nb_samples,
                                                 out_sample_format,
                                                 0);
    
    if (!resampled_data) {
      return;
    }
    
    int64_t progressive_delay = swr_get_delay(context, avfrm->sample_rate) + avfrm->nb_samples;
    int out_nb_samples = av_rescale_rnd(progressive_delay, out_sample_rate, avfrm->sample_rate, AV_ROUND_UP);
    
    if (out_nb_samples > max_out_nb_samples) {
      av_free(resampled_data[0]);
      
      av_samples_alloc(resampled_data,
                       &out_linesize,
                       out_nb_channels,
                       out_nb_samples,
                       out_sample_format,
                       1);
    }

    out_nb_samples = swr_convert(context, resampled_data, out_nb_samples, (const uint8_t **)avfrm->data, avfrm->nb_samples);

    resampled_data_size = av_samples_get_buffer_size(&out_linesize,
                                                     out_nb_channels,
                                                     out_nb_samples,
                                                     out_sample_format,
                                                     1);
    
    AVFrame *frame_resampled = av_frame_alloc();
    frame_resampled->data[0] = (uint8_t *)av_malloc(resampled_data_size);
    memcpy(frame_resampled->data[0], resampled_data[0], resampled_data_size);
    frame_resampled->linesize[0] = resampled_data_size;
    frame_resampled->opaque = frame_resampled->data[0];
    frame_resampled->display_picture_number = avfrm->display_picture_number;
    frame_resampled->pts = avfrm->pts;
    frame_resampled->nb_samples = out_nb_samples;
    frame_resampled->format = out_sample_format;
    frame_resampled->sample_rate = out_sample_rate;

    PipelineMessage frmMsg;
    frmMsg["type"]  = "media_frame";
    frmMsg["frame"] = frame_resampled;
    deliverPipelineMessage(frmMsg);

    av_freep(&resampled_data[0]);
    av_freep(&resampled_data);
  }
};

class SDLSpeaker: public RenderDriver {
  typedef struct {
    AVFrame *frame;
    uint8_t *rptr;
    int      remainBytes;
  } AudioFrameItem;
  
public:
  SDLSpeaker(AVStream *stream, TimeSync *timeSync) {
    this->stream     = stream;
    this->timeSync   = lms::retain(timeSync);
    this->frameItems = new FramesBuffer<AudioFrameItem *>;
    this->totalSamples = 0;
    
    SDL_AudioSpec request_specs, respond_specs;
    request_specs.freq     = stream->codecpar->sample_rate;
    request_specs.format   = AUDIO_S16;
    request_specs.channels = av_get_channel_layout_nb_channels(stream->codecpar->channel_layout);
    request_specs.silence  = 0;

    // stream->codecpar->frame_size表示每个音频帧中包含的样本数，这么做可以尽可能保证每个数据回调都正好消耗掉一个音频帧
    request_specs.samples  = stream->codecpar->frame_size;
    request_specs.callback = (SDL_AudioCallback) loadAudioData;
    request_specs.userdata = this;

    speakerId = SDL_OpenAudioDevice(NULL,
                                    0,
                                    &request_specs,
                                    &respond_specs,
                                    SDL_AUDIO_ALLOW_FORMAT_CHANGE);

  }
  
  ~SDLSpeaker() {
    SDL_CloseAudioDevice(speakerId);

    lms::release(timeSync);
    lms::release(frameItems);
  }
  
protected:
  void didReceivePipelineMessage(const PipelineMessage& msg) override {
    auto avfrm = (AVFrame *)msg.at("frame").value.ptr;
    AudioFrameItem *afi = new AudioFrameItem { avfrm, avfrm->data[0], avfrm->linesize[0] };
    frameItems->pushBack(afi);
    
    totalSamples += avfrm->linesize[0];
  }
  
private:
  static void loadAudioData(SDLSpeaker *self, Uint8 *data, int len) {
    memset(data, 0, len);
    
    while(len > 0) {
      if (self->frameItems->count() < SDLSpeaker::IdealCachingFrames) {
        fireEvent("should_load_next_frame", self, {
          {"stream_object", self->stream}
        });
      }
      
      AudioFrameItem *afi = self->frameItems->popFront();
      if (afi == nullptr) {
        LMSLogWarning("No audio frame!");
        break;
      }
      
      AVFrame *frame = afi->frame;

      double ts = frame->pts * av_q2d(self->stream->time_base);
      self->timeSync->updateTimePivot(ts);
      
      LMSLogVerbose("Rendering audio frame | ts:%.2lf, pts:%llu, remain_bytes:%u, remains_frames:%lu",
                    ts, frame->pts, afi->remainBytes, self->frameItems->count());
            
      int bytesToWrite = std::min(afi->remainBytes, len);
      memcpy(data, afi->rptr, bytesToWrite);
      
      len -= bytesToWrite;
      afi->remainBytes -= bytesToWrite;
      afi->rptr += bytesToWrite;
      
      self->totalSamples -= bytesToWrite;

      // 如果frame中剩余了数据未消费，则重新放入待处理队列
      if (afi->remainBytes > 0) {
        self->frameItems->pushFront(afi);
      } else {
        av_freep(&frame->data[0]);
        av_frame_free(&frame);
        delete afi;
      }
    }
  }
  
  double cachedPlayingTime() override {
    int out_nb_channels = av_get_channel_layout_nb_channels(stream->codecpar->channel_layout);
    return (double)totalSamples / (double)stream->codecpar->sample_rate / out_nb_channels / 2 /* bytes per sample */;
  }

private:
  void start() override {
    SDL_PauseAudioDevice(speakerId, 0);
  }
  
  void stop() override {
    SDL_PauseAudioDevice(speakerId, 1);
  }
  
private:
  SDL_AudioDeviceID speakerId;
  
  AVStream *stream;
  TimeSync *timeSync;
  FramesBuffer<AudioFrameItem *> *frameItems;
  std::atomic<uint32_t> totalSamples;
  
  constexpr static int IdealCachingFrames = 10;
};

class VideoRenderDriver : public RenderDriver {
public:
  VideoRenderDriver(AVStream *stream, Cell *videoRender, TimeSync *timeSync) {
    this->stream     = stream;
    this->render     = lms::retain(videoRender);
    this->timeSync   = lms::retain(timeSync);
    this->frameMutex = SDL_CreateMutex();
    this->nextFrame  = nullptr;
  }
  
  ~VideoRenderDriver() {
    SDL_DestroyMutex(frameMutex);
    lms::release(render);
    lms::release(timeSync);
  }
 
  void start() override {
    nextFrame = nullptr;

    render->start();
    
    double fps = av_q2d(stream->avg_frame_rate);
    double spf = 1.0 / fps; // second per frame, also timer interval
    
    fpsTimer = scheduleTimer("VideoRenderDriver", spf, [this, spf] {
      double playingTime = timeSync->getPlayingTime();
      if (playingTime < 0) {
        return;
      }
      
      AVFrame *frame;
      
      EventParams loadingParams = {
        {"stream_object", this->stream}
      };
      
      while(true) {
        frame = nullptr;
        SDL_LockMutex(frameMutex);
        {
          if (nextFrame) {
            frame = nextFrame;
            nextFrame = nullptr;
          }
        }
        SDL_UnlockMutex(frameMutex);
        
        if (frame == nullptr) {
          lms::fireEvent("should_load_next_frame", this, loadingParams);
          LMSLogWarning("No video frame!");
          return;
        }
        
        double frameTime = frame->best_effort_timestamp * av_q2d(stream->time_base);

        // deviation > 0 表示当前视频帧的应播时间大于当前播放时间（待播帧）
        // deviation < 0 表示当前视频帧的应播时间小于当前播放时间（迟滞帧），超过一定时间（tollerance）则认为是过期帧
        double deviation = frameTime - playingTime;

        LMSLogVerbose("Video frame popped | pts:%lld, frameTime:%.2lf, playingTime:%.2lf, deviation:%.3lf(%.2lf frames)",
                      frame->pts, frameTime, playingTime, deviation, deviation / spf);

        double tollerance = spf / 2.0;
        if (deviation < -tollerance) {
          // 丢弃过期帧，继续下一帧（如果有）的处理
          LMSLogWarning("Video frame dropped");
          av_frame_unref(frame);

          lms::fireEvent("should_load_next_frame", this, loadingParams);
          continue;
        } else
        if (deviation > tollerance) {
          SDL_LockMutex(frameMutex);
          {
            nextFrame = frame;
          }
          SDL_UnlockMutex(frameMutex);

          // 如果队列头的帧都未到播放时间，应认为后续帧也肯定未到播放时间，所以应直接退出渲染流程
          LMSLogWarning("Video frame refilled");

          frame = nullptr;
          return;
        } else {
          lms::fireEvent("should_load_next_frame", this, loadingParams);
          break;
        }
      }
      
      assert(frame != nullptr);
      
      if (render) {
        PipelineMessage msg;
        msg["type"]  = "media_frame";
        msg["frame"] = frame;
        render->didReceivePipelineMessage(msg);
        av_frame_unref(frame);
      }

    });
  }
  
  void stop() override {
    invalidateTimer(fpsTimer);
    lms::release(fpsTimer);
    
    render->stop();
    
    // TODO: 释放缓存帧
    nextFrame = nullptr;
  }
  
  double cachedPlayingTime() override {
    // TODO:
    return 0;
  }
  
  void didReceivePipelineMessage(const PipelineMessage& msg) override {
    auto avfrm = (AVFrame *)msg.at("frame").value.ptr;
    
    SDL_LockMutex(frameMutex);
    {
      if (nextFrame) {
        av_frame_free(&nextFrame);
      }

      nextFrame = av_frame_clone(avfrm);
    }
    SDL_UnlockMutex(frameMutex);
  }
 
private:
  AVStream *stream;
  Cell     *render;
  Timer    *fpsTimer;
  TimeSync *timeSync;
  
  SDL_mutex *frameMutex;
  AVFrame   *nextFrame;
};

class Stream : public Cell {
public:
  Stream(const StreamMeta &meta, Cell *decoder, Cell *resampler, RenderDriver *renderDriver) {
    this->meta         = meta;
    this->streamObject = meta.at("stream_object").value.ptr;
    this->renderDriver = lms::retain(renderDriver);
    this->resampler    = lms::retain(resampler);
    this->decoder      = lms::retain(decoder);
  }

  ~Stream() {
    lms::release(resampler);
    lms::release(decoder);
    lms::release(renderDriver);
  }
  
  void start() override {
    if (resampler) {
      decoder->addReceiver(resampler);
      resampler->addReceiver(renderDriver);
    } else {
      decoder->addReceiver(renderDriver);
    }

    renderDriver->start();
    decoder->start();
  }
  
  void stop() override {
    decoder->stop();
    renderDriver->stop();
    
    if (resampler) {
      decoder->removeReceiver(resampler);
      resampler->removeReceiver(renderDriver);
    } else {
      decoder->removeReceiver(renderDriver);
    }
  }
  
  void didReceivePipelineMessage(const PipelineMessage& msg) override {
    if (msg.at("stream_object").value.ptr == streamObject) {
      decoder->didReceivePipelineMessage(msg);
    }
  }
  
private:
  StreamMeta meta;
  void *streamObject;
  Cell *decoder;
  Cell *resampler;
  RenderDriver *renderDriver;
};

class SourceDriver : virtual public Object {
public:
  SourceDriver(MediaSource *source) {
    this->source = lms::retain(source);
  }
  
  ~SourceDriver() {
    lms::release(source);
  }

public:
  void start() {
    LMSLogInfo("Start coordinator");

    obsDUP = addEventObserver("did_update_packets", nullptr, this, (EventCallback)onEventDidUpdatePackets);
    
    fireEvent("load_packets", this, {
      { "count", 100 }
    });
  }
  
  void stop() {
    LMSLogInfo("Stop coordinator");
    
    removeEventObserver(obsDUP);
    obsDUP = nullptr;
  }
  
private:
  static void onEventDidUpdatePackets(SourceDriver *self, const char *ename, void *sender, const EventParams& p) {
    int type  = variantsGetUInt(p, "type");
    int dec   = variantsGetUInt(p, "decrement");
    
    if (type == 2 && dec > 0) {
      fireEvent("load_packets", self, {
        { "count", (uint64_t)dec }
      });
    }
  }

private:
  MediaSource *source;
  void        *obsDUP;
};


Player::Player(MediaSource *mediaSource, Cell *vrender) {
  this->source = lms::retain(mediaSource);
  this->vrender     = lms::retain(vrender);
  this->coordinator = new SourceDriver(mediaSource);
  this->timesync    = new TimeSync;
  this->vstream     = nullptr;
  this->astream     = nullptr;
}

Player::~Player() {
  assert(!vstream);
  assert(!astream);

  lms::release(coordinator);
  lms::release(timesync);
  lms::release(source);
  lms::release(vrender);
}

void Player::play() {
  sync(mainQueue(), [this] {
    doPlay();
  });
}

void Player::stop() {
  sync(mainQueue(), [this] {
    doStop();
  });
}

void Player::doPlay() {
  LMSLogInfo(nullptr);

  // 必须先加载source的数据才能获取当中的元信息
  if (source->open() != 0) {
    return;
  }
  
  RenderDriver *theDriver = nullptr;

  auto nbStreams = source->numberOfStreams();
  int  streamId = -1;
  for (int i = 0; i < nbStreams; i += 1) {
    auto meta   = source->getStreamMeta(i);
    auto mtype  = meta.at("media_type").value.u;
    auto stream = (AVStream *)meta.at("stream_object").value.ptr;

    if (mtype == MediaTypeVideo) {
      VideoRenderDriver *driver = new VideoRenderDriver(stream, vrender, timesync);
      
      Cell *decoder = createDecoder(meta);
      vstream = new Stream(meta, decoder, nullptr, driver);

      vrender->configure(meta);

      lms::release(driver);
      lms::release(decoder);
    } else if (mtype == MediaTypeAudio) {
      SDLSpeaker *speaker = new SDLSpeaker(stream, timesync);
      theDriver = speaker;

      Cell *decoder = createDecoder(meta);

      SDLAudioResampler *resampler = new SDLAudioResampler(stream);
      astream = new Stream(meta, decoder, resampler, speaker);
      
      lms::release(resampler);
      lms::release(decoder);
      lms::release(speaker);
    }
  }
  
  if (vstream == nullptr && astream == nullptr) {
    LMSLogError("Failed creating video & audio stream");
    return;
  }
  
  // [#55 避免视频的头几帧被丢弃]
  // 在player启动播放时，会立即开始帧渲染。但是视频的播放可能早于处理音频第一帧的时间。而播放时间轴的初始化是在音频首帧播放
  // 时设置的。这会导致部分部分视频帧被丢弃。所以，在astream, vstream启动前，应手动重置播放时间轴为无效状态。直到音频首帧加载后将时间轴
  // 置零为止。
  timesync->updateTimePivot(InvalidPlayingTime);
  
  if (vstream) {
    source->addReceiver(vstream);
    vstream->start();
  }
  
  if (astream) {
    source->addReceiver(astream);
    astream->start();
  }
  
  coordinator->start();
}

void Player::doStop() {
  LMSLogInfo(nullptr);
  
  coordinator->stop();
  
  if (astream) {
    astream->stop();
    source->removeReceiver(astream);
  }
  
  if (vstream) {
    vstream->stop();
    source->removeReceiver(vstream);
  }

  source->close();

  lms::release(vstream);
  vstream = nullptr;
  
  lms::release(astream);
  astream = nullptr;
}

} // namespace lms
