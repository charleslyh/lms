#include "Player.h"
#include "MediaSource.h"
#include "Decoder.h"
#include "Render.h"
#include "Buffer.h"
#include "Logger.h"
#include "Runtime.h"
#include "Cell.h"
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

class RenderDriver;

class RenderDriverDelegate : virtual public Object {
public:
  virtual void willRunRenderLoop(RenderDriver *driver) {}
  virtual void didRunRenderLoop(RenderDriver *driver) {}
};

class RenderDriver : public FrameAcceptor {
public:
  ~RenderDriver() {
    lms::release(this->delegate);
  }
  
  virtual void start(const StreamMeta& meta) = 0;
  virtual void stop() = 0;
  
  virtual double cachedPlayingTime() = 0;
  
  void setDelegate(RenderDriverDelegate *delegate) {
    lms::release(this->delegate);
    this->delegate = lms::retain(delegate);
  }
  
protected:
  RenderDriverDelegate *delegate = nullptr;
};

class Resampler: public FrameAcceptor, public FrameSource {};


class SDLAudioResampler: public Resampler {
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
    ret = av_opt_set_int(context,        "in_channel_layout", in_channel_layout, 0);
    ret = av_opt_set_int(context,        "in_sample_rate",    stream->codecpar->sample_rate, 0);
    ret = av_opt_set_sample_fmt(context, "in_sample_fmt",     (enum AVSampleFormat)stream->codecpar->format, 0);

    ret = av_opt_set_int(context, "out_channel_layout", out_channel_layout, 0);
    ret = av_opt_set_int(context, "out_sample_rate",    out_sample_rate,    0);
    ret = av_opt_set_int(context, "out_sample_fmt",     out_sample_format,  0);

    ret = swr_init(context);
  }

  ~SDLAudioResampler() {
    swr_free(&this->context);
  }

public:
  void didReceiveFrame(Frame *frame) override {
    AVFrame *avfrm = (AVFrame *)frame;
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

    deliverFrame((Frame *)frame_resampled);

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
    request_specs.samples  = 1024;
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
  void didReceiveFrame(Frame *frame) override {
    auto avfrm = (AVFrame *)frame;
    AudioFrameItem *afi = new AudioFrameItem { avfrm, avfrm->data[0], avfrm->linesize[0] };
    frameItems->pushBack(afi);
    
    totalSamples += avfrm->linesize[0];
  }
  
private:
  static void loadAudioData(SDLSpeaker *self, Uint8 *data, int len) {
    if (self->delegate) {
      self->delegate->willRunRenderLoop(self);
    }

    memset(data, 0, len);
    
    while(len > 0) {
      AudioFrameItem *afi = self->frameItems->popFront();
      if (afi == nullptr) {
        LMSLogWarning("No audio frame available!");
        break;
      }
      
      AVFrame *frame = afi->frame;

      double ts = frame->pts * av_q2d(self->stream->time_base);
      self->timeSync->updateTimePivot(ts);
      
      LMSLogVerbose("Start rendering audio frame | ts:%.2lf, pts:%llu, remains: %lu",
                    ts, frame->pts, self->frameItems->count());
            
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
    
    if (self->delegate) {
      self->delegate->didRunRenderLoop(self);
    }
  }
  
  double cachedPlayingTime() override {
    int out_nb_channels = av_get_channel_layout_nb_channels(stream->codecpar->channel_layout);
    return (double)totalSamples / (double)stream->codecpar->sample_rate / out_nb_channels / 2 /* bytes per sample */;
  }

private:
  void start(const StreamMeta &meta) override {
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
};

class VideoRenderDriver : public RenderDriver {
public:
  VideoRenderDriver(AVStream *stream, Cell *videoRender, TimeSync *timeSync) {
    this->stream   = stream;
    this->render   = lms::retain(videoRender);
    this->timeSync = lms::retain(timeSync);
    this->buffer   = new FramesBuffer<AVFrame *>;
  }
  
  ~VideoRenderDriver() {
    lms::release(render);
    lms::release(timeSync);
    lms::release(buffer);
  }
  
  void start(const StreamMeta& meta) override {
    render->start();
    
    double fps = av_q2d(stream->avg_frame_rate);
    double spf = 1.0 / fps; // second per frame, also timer interval
    
    fpsTimer = scheduleTimer("FPSTimer", spf, [this, spf] {
      double playingTime = timeSync->getPlayingTime();
      if (playingTime < 0) {
        return;
      }
      
      AVFrame *frame = nullptr;

      // 从buffer中循环取出匹配当前播放时间的图像帧，或者buffer为空，则终止
      while(true) {
        frame = (AVFrame *)buffer->popFront();

        if (frame == nullptr) {
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
          continue;
        } else if (deviation > tollerance) {
          // 该帧尚未到播放时间，将其重入等待队列
          buffer->pushFront(frame);
          
          // 如果队列头的帧都未到播放时间，应认为后续帧也肯定未到播放时间，所以应直接退出渲染流程
          LMSLogWarning("Video frame refilled");
          return;
        } else {
          break;
        }
      }

      if (frame == nullptr) {
        return;
      }

      if (delegate) {
        delegate->willRunRenderLoop(this);
      }

      if (render) {
        CellMessage msg;
        msg["media-frame"] = Variant((void *)frame);
        render->didReceiveCellMessage(msg);
        av_frame_unref(frame);
      }

      if (delegate) {
        delegate->didRunRenderLoop(this);
      }
    });
  }
  
  void stop() override {
    invalidateTimer(fpsTimer);
    lms::release(fpsTimer);
    
    render->stop();
  }
  
  double cachedPlayingTime() override {
    double spf = 1.0 / av_q2d(stream->avg_frame_rate);
    return buffer->count() * spf;
  }
  
  void didReceiveFrame(Frame *frame) override {
    buffer->pushBack(av_frame_clone((AVFrame *)frame));
  }
 
private:
  AVStream *stream;
  Cell     *render;
  Timer    *fpsTimer;
  TimeSync *timeSync;
  FramesBuffer<AVFrame *> *buffer;
};

class Stream : virtual public Object {
public:
  Stream(const StreamMeta& meta, PassiveMediaSource *source, Decoder *decoder, Resampler *resampler, RenderDriver *renderDriver) {
    this->meta         = meta;
    this->source       = lms::retain(source);
    this->renderDriver = lms::retain(renderDriver);
    this->resampler    = lms::retain(resampler);
    this->decoder      = lms::retain(decoder);
  }

  ~Stream() {
    lms::release(source);
    lms::release(resampler);
    lms::release(decoder);
    lms::release(renderDriver);
  }
  
  void start() {
    source->addPacketAcceptor(meta.streamId, decoder);
    
    if (resampler) {
      decoder->addFrameAcceptor(resampler);
      resampler->addFrameAcceptor(renderDriver);
    } else {
      decoder->addFrameAcceptor(renderDriver);
    }

    renderDriver->start(meta);
    decoder->start();
  }
  
  void stop() {
    decoder->stop();
    renderDriver->stop();
    
    if (resampler) {
      decoder->removeFrameAcceptor(resampler);
      resampler->removeFrameAcceptor(renderDriver);
    } else {
      decoder->removeFrameAcceptor(renderDriver);
    }

    source->removePacketAcceptor(decoder);
  }
  
private:
  StreamMeta meta;
  PassiveMediaSource *source;
  Decoder            *decoder;
  Resampler          *resampler;
  RenderDriver       *renderDriver;
};

class Coordinator : public RenderDriverDelegate {
public:
  Coordinator(PassiveMediaSource *source) {
    this->source = lms::retain(source);
    this->isRunning = false;
  }
  
  ~Coordinator() {
    lms::release(source);
  }

public:
  void start() {
    isRunning = true;

    this->source->loadPackets(10);
  }
  
  void stop() {
    isRunning = false;
  }
 
  void didRunRenderLoop(RenderDriver* driver) override {
    LMSLogVerbose("isRunning: %d, RenderDriver: %p, cachedPlayingTime: %.2lf", isRunning.load(), driver, driver->cachedPlayingTime());
    
    if (!isRunning) {
      return;
    }
    
    if (driver->cachedPlayingTime() < 1.0) {
      this->source->loadPackets(5);
    }
  }
  
private:
  PassiveMediaSource *source;
  std::atomic<bool> isRunning;
};


Player::Player(PassiveMediaSource *s, Cell *vrender) {
  this->source      = lms::retain(s);
  this->vrender     = lms::retain(vrender);
  this->coordinator = new Coordinator(s);
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
  LMSLogInfo(nullptr);

  // 必须先加载source的数据才能获取当中的元信息
  if (source->open() != 0) {
    return;
  }
  
  auto nbStreams = source->numberOfStreams();
  int  streamId = -1;
  for (int i = 0; i < nbStreams; i += 1) {
    auto meta   = source->streamMetaAt(i);
    auto smi    = source->getStreamMeta(i);
    auto stream = (AVStream *)meta.data;

    if (meta.mediaType == MediaTypeVideo) {
      VideoRenderDriver *driver = new VideoRenderDriver(stream, vrender, timesync);
      Decoder *decoder = createDecoder(meta);

      driver->setDelegate(coordinator);
      vstream = new Stream(meta, source, decoder, nullptr, driver);

      vrender->configure(smi);

      lms::release(driver);
      lms::release(decoder);
    } else if (meta.mediaType == MediaTypeAudio) {
      SDLSpeaker *speaker = new SDLSpeaker(stream, timesync);
      speaker->setDelegate(coordinator);

      Decoder *decoder = createDecoder(meta);

      SDLAudioResampler *resampler = new SDLAudioResampler(stream);
      astream = new Stream(meta, source, decoder, resampler, speaker);
      
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
  
  if (vstream) vstream->start();
  if (astream) astream->start();
  coordinator->start();
}

void Player::stop() {
  LMSLogInfo(nullptr);
  
  coordinator->stop();
    
  source->close();

  if (astream) astream->stop();
  if (vstream) vstream->stop();

  lms::release(vstream);
  vstream = nullptr;
  
  lms::release(astream);
  astream = nullptr;
}

} // namespace lms
