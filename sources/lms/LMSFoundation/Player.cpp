#include "LMSFoundation/Player.h"
#include "LMSFoundation/MediaSource.h"
#include "LMSFoundation/Decoder.h"
#include "LMSFoundation/Render.h"
#include "LMSFoundation/Buffer.h"
#include "LMSFoundation/Logger.h"
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
    timePivot  = time;
    tickPivot = SDL_GetTicks();
  }
  
private:
  double   timePivot;
  uint64_t tickPivot;
};

class RenderDriver;

class RenderDriverDelegate : virtual public Object {
public:
  virtual void willRunRenderLoop(RenderDriver *driver, uint64_t frameIndex) {}
  virtual void didRunRenderLoop(RenderDriver *driver, uint64_t frameIndex) {}
};

class RenderDriver : public FrameAcceptor {
public:
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
  int out_channel_layout;
  int out_sample_rate;
  AVSampleFormat out_sample_format;

public:
  SDLAudioResampler(AVStream *stream) {
    this->stream = stream;
    this->out_sample_format  = AV_SAMPLE_FMT_S16;
    this->out_sample_rate    = stream->codecpar->sample_rate;
    this->out_channel_layout = stream->codecpar->channel_layout;
  }

public:
  void didReceiveFrame(Frame *frame) override {
    AVFrame *avfrm = (AVFrame *)frame;
    
    SwrContext *context = nullptr;
    int ret = 0;
    int64_t in_channel_layout  = stream->codecpar->channel_layout;
    int in_nb_channels = stream->codecpar->channels;
    int in_nb_samples = 0;
    int out_linesize = 0;
    int max_out_nb_samples = 0;
    uint8_t **resampled_data = NULL;
    int resampled_data_size = 0;
    int out_nb_channels = av_get_channel_layout_nb_channels(out_channel_layout);
    
    context = swr_alloc();
    
    // get input audio channels
    bool channels_matches_layout = (in_nb_channels == av_get_channel_layout_nb_channels(in_channel_layout));
    if (!channels_matches_layout) {
      in_channel_layout = av_get_default_channel_layout(in_nb_channels);
    }
    
    ret = av_opt_set_int(context, "in_channel_layout", in_channel_layout, 0);
    ret = av_opt_set_int(context, "in_sample_rate",    stream->codecpar->sample_rate, 0);
    ret = av_opt_set_sample_fmt(context, "in_sample_fmt", (enum AVSampleFormat)stream->codecpar->format, 0);

    ret = av_opt_set_int(context, "out_channel_layout", out_channel_layout, 0);
    ret = av_opt_set_int(context, "out_sample_rate", out_sample_rate, 0);
    ret = av_opt_set_int(context, "out_sample_fmt", out_sample_format, 0);
    
    ret = swr_init(context);
    
    max_out_nb_samples = av_rescale_rnd(avfrm->nb_samples,
                                        out_sample_rate,
                                        avfrm->sample_rate,
                                        AV_ROUND_UP);
    
    ret = av_samples_alloc_array_and_samples(&resampled_data,
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
    swr_free(&context);
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
    this->stream   = stream;
    this->timeSync = timeSync;
    this->mutex    = SDL_CreateMutex();
    
    SDL_AudioSpec request_specs, respond_specs;
    request_specs.freq     = stream->codecpar->sample_rate;
    request_specs.format   = AUDIO_S16;
    request_specs.channels = av_get_channel_layout_nb_channels(stream->codecpar->channel_layout);
    request_specs.silence  = 0;
    request_specs.samples  = 1024;
    request_specs.callback = (SDL_AudioCallback) loadAudioData;
    request_specs.userdata = this;
    
    SDL_AudioDeviceID speakerId = SDL_OpenAudioDevice(NULL,
                                                      0,
                                                      &request_specs,
                                                      &respond_specs,
                                                      SDL_AUDIO_ALLOW_FORMAT_CHANGE);

    SDL_PauseAudioDevice(speakerId, 0);
  }
  
protected:
  void didReceiveFrame(Frame *frame) override {
    auto avfrm = (AVFrame *)frame;
    AudioFrameItem *afi = new AudioFrameItem { avfrm, avfrm->data[0], avfrm->linesize[0] };
    pushFrame(afi);
  }
  
private:
  static void loadAudioData(SDLSpeaker *self, Uint8 *data, int len) {
    if (self->delegate) {
      self->delegate->willRunRenderLoop(self, 0);
    }

    memset(data, 0, len);

    while(len > 0) {
      AudioFrameItem *afi = self->popFrame();
      if (afi == nullptr) {
        break;
      }
      
      AVFrame *frame = afi->frame;
      double ts = frame->pts * av_q2d(self->stream->time_base);
      
      self->timeSync->updateTimePivot(ts);

      LMSLogVerbose("Start rendering audio frame | ts:%.2lf, pts:%llu", ts, frame->pts);
            
      int bytesToWrite = std::min(afi->remainBytes, len);
      memcpy(data, afi->rptr, bytesToWrite);
      
      len -= bytesToWrite;
      afi->remainBytes -= bytesToWrite;
      afi->rptr += bytesToWrite;

      // 如果frame中剩余了数据未消费，则重新放入待处理队列
      if (afi->remainBytes > 0) {
        self->refillFrame(afi);
      } else {
        av_freep(&frame->data[0]);
        av_frame_free(&frame);
        delete afi;
      }
    }
    
    if (self->delegate) {
      self->delegate->didRunRenderLoop(self, 0);
    }
  }
  
  void pushFrame(AudioFrameItem *afi) {
    SDL_LockMutex(this->mutex);
    {
      frames.push_back(afi);
    }
    SDL_UnlockMutex(this->mutex);
  }
  
  void refillFrame(AudioFrameItem *afi) {
    SDL_LockMutex(this->mutex);
    {
      frames.push_front(afi);
    }
    SDL_UnlockMutex(this->mutex);
  }

  AudioFrameItem *popFrame() {
    AudioFrameItem *afi = nullptr;
    SDL_LockMutex(mutex);
    {
      if (!frames.empty()) {
        afi = frames.front();
        frames.pop_front();
        LMSLogWarning("Audio popped, remains: %lu", frames.size());
      } else {
        LMSLogWarning("No audio frame available!");
      }
    }
    SDL_UnlockMutex(this->mutex);
    
    return afi;
  }
  
  double cachedPlayingTime() override {
    double time = frames.size() * av_q2d(stream->time_base);
    return time;
  }

private:
  SDL_mutex *mutex;
  std::list<AudioFrameItem *> frames;

  void start(const StreamMeta &meta) override {
  }
  
  void stop() override {
  }
  
private:
  AVStream *stream;
  TimeSync *timeSync;
};

class VideoRenderDriver : public RenderDriver {
public:
  VideoRenderDriver(AVStream *stream, Render *videoRender, TimeSync *timeSync) {
    this->stream   = stream;
    this->render   = videoRender;
    this->timeSync = timeSync;
    this->buffer   = new FramesBuffer<AVFrame *>;
  }
  
  ~VideoRenderDriver() {
    lms::release(buffer);
  }
  
  void start(const StreamMeta& meta) override {
    render->start(meta);
    
    double fps = av_q2d(stream->avg_frame_rate);
    double spf = 1.0 / fps; // second per frame
    
    lms::dispatchAsyncPeriodically(mainQueue(), fps, [this, spf] {
      AVFrame *frame = nullptr;

      while(true) {
        frame = (AVFrame *)buffer->popFrame();
        if (frame == nullptr) {
          break;
        }

        double frameTime = frame->best_effort_timestamp * av_q2d(stream->time_base);
        double playingTime = timeSync->getPlayingTime();

        // deviation > 0 表示视频播放领先于音频的播放时间
        // deviation < 0 表示视频播放落后于音频的播放时间
        double deviation = frameTime - playingTime;

        LMSLogVerbose("Video frame popped | pts:%lld, frameTime:%.2lf, playingTime:%.2lf, deviation:%.3lf(%.2lf frames)",
                      frame->pts, frameTime, playingTime, deviation, deviation / spf);
        
        if (deviation > -spf) {
          break;
        } else {
          // 当deviation小于一帧的时间间隔时，可以认为它已经远落后于播放进度，因此应予以丢弃，并立即尝试处理下一帧
          LMSLogWarning("Video frame dropped | pts:%lld, deviation:%.3lf(%.2lf frames)", frame->pts, deviation, deviation / spf);
        }
      }

      if (frame == nullptr) {
        LMSLogWarning("No video frame available!");
        return;
      }

      dispatchAsync(mainQueue(), [this] {
        delegate->willRunRenderLoop(this, this->frameIndex);
      });

      if (render) {
        render->didReceiveFrame(frame);
      }
      
      if (delegate) {
        dispatchAsync(mainQueue(), [this] () {
          delegate->didRunRenderLoop(this, this->frameIndex);
        });
      }
      
      this->frameIndex += 1;
    });
  }
  
  void stop() override {
    LMSLogError("TODO: Cancel periodic job");
    
    render->stop();
  }
  
  double cachedPlayingTime() override {
    double spf = 1.0 / av_q2d(stream->avg_frame_rate);
    return buffer->numberOfCachedFrames() * spf;
  }
  
  void didReceiveFrame(Frame *frame) override {
    buffer->pushBack((AVFrame *)frame);
  }
 
private:
  AVStream *stream;
  Render   *render;
  uint64_t frameIndex;
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
  }
  
  ~Coordinator() {
    lms::release(source);
  }

public:
  void preload() {
    this->source->loadPackets(100);
  }
 
  void didRunRenderLoop(RenderDriver* driver, uint64_t frameIndex) override {
    LMSLogVerbose(nullptr);
    if (driver->cachedPlayingTime() < 1.0) {
      this->source->loadPackets(10);
    }
  }
  
private:
  PassiveMediaSource *source;
};


Player::Player(PassiveMediaSource *s, Render *vrender) {
  this->source      = lms::retain(s);
  this->vrender     = lms::retain(vrender);
  this->coordinator = new Coordinator(s);
  this->timesync    = new TimeSync;
  this->vstream     = nullptr;
  this->astream     = nullptr;
}

Player::~Player() {
  lms::release(coordinator);
  lms::release(timesync);
  lms::release(vstream);
  lms::release(astream);
  lms::release(source);
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
    auto stream = (AVStream *)meta.data;

    if (meta.mediaType == MediaTypeVideo) {
      VideoRenderDriver *driver = autoRelease(new VideoRenderDriver(stream, vrender, timesync));
      driver->setDelegate(coordinator);

      Decoder *decoder = autoRelease(createDecoder(meta));

      vstream = new Stream(meta, source, decoder, nullptr, driver);
    } else if (meta.mediaType == MediaTypeAudio) {
      SDLSpeaker *speaker = new SDLSpeaker(stream, timesync);
      speaker->setDelegate(coordinator);

      Decoder *decoder = autoRelease(createDecoder(meta));

      SDLAudioResampler *resampler = autoRelease(new SDLAudioResampler(stream));
      astream = new Stream(meta, source, decoder, resampler, speaker);
    }
  }
  
  if (vstream == nullptr && astream == nullptr) {
    LMSLogError("Failed creating video & audio stream");
    return;
  }
  
  vstream->start();
  astream->start();

  coordinator->preload();
}

void Player::stop() {
  LMSLogInfo(nullptr);
  
  if (astream != nullptr) {
    astream->stop();
  }
  
  if (vstream != nullptr) {
    vstream->stop();
  }
  source->close();

  lms::release(vstream);
  lms::release(coordinator);
}

} // namespace lms
