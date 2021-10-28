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

namespace lms {

class RenderDriver;

class RenderDriverDelegate : virtual public Object {
public:
  virtual void willRunRenderLoop(RenderDriver *driver) {}
  virtual void didRunRenderLoop(RenderDriver *driver, bool didRenderFrame) {}
};

class RenderDriver : public FramesBuffer {
public:
  virtual void start(const StreamMeta& meta) = 0;
  virtual void stop() = 0;
  
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
public:
  SDLSpeaker(AVStream *stream) {
    this->mutex = SDL_CreateMutex();
    this->cond  = SDL_CreateCond();
    
    SDL_AudioSpec request_specs, respond_specs;
    request_specs.freq     = stream->codecpar->sample_rate;
    request_specs.format   = AUDIO_S16;
    request_specs.channels = av_get_channel_layout_nb_channels(stream->codecpar->channel_layout);
    request_specs.silence  = 0;
    request_specs.samples  = 1024;
    request_specs.callback = (SDL_AudioCallback) load_audio_data;
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
    SDL_LockMutex(this->mutex);
    {
      frames.push_back((AVFrame *)frame);
      SDL_CondSignal(this->cond);
    }
    SDL_UnlockMutex(this->mutex);
  }
  
  
  
private:
  static void load_audio_data(SDLSpeaker *self, Uint8 *data, int len) {
    memset(data, 0, len);

    while(len > 0) {
      AVFrame *frame = self->pop_frame();
      
      LMSLogVerbose("Audio frame timestamp: %lu", frame->pts);
      
      if (frame->linesize[0] < len) {
        continue;
      }
      
      int bytes_to_write = std::min(frame->linesize[0], len);
      memcpy(data, frame->opaque, bytes_to_write);
      
              
      len -= bytes_to_write;
      frame->linesize[0] -= bytes_to_write;
      frame->opaque = ((uint8_t *)frame->opaque) + bytes_to_write;

      // 如果frame中剩余了数据未消费，则重新放入待处理队列
      if (frame->linesize[0] > 0) {
        self->refill_frame(frame);
      } else {
        av_freep(&frame->data[0]);
        av_frame_free(&frame);
      }
    }
  }
  
  AVFrame *pop_frame() {
    AVFrame *frame = nullptr;

    SDL_LockMutex(mutex);

    for (;;) {
      if (frames.size() > 0) {
        frame = frames.front();
        frames.pop_front();
        LMSLogWarning("Audio popped, remains: %lu", frames.size());
        break;
      } else {
        // unlock mutex and wait for cond signal, then lock mutex again
        LMSLogWarning("Not enough audio data!");
        SDL_CondWaitTimeout(this->cond, this->mutex, 10);
      }
    }

    SDL_UnlockMutex(this->mutex);
    
    return frame;
  }
  
  void refill_frame(AVFrame *frame) {
    SDL_LockMutex(this->mutex);
    {
      frames.push_front(frame);
    }
    SDL_UnlockMutex(this->mutex);
  }

private:
  SDL_mutex *mutex;
  SDL_cond *cond;
  std::list<AVFrame *> frames;

  void start(const StreamMeta &meta) override {
  }
  
  void stop() override {
  }
  
};

class VideoRenderDriver : public RenderDriver {
public:
  VideoRenderDriver(int maxBufferingFrames, Render *videoRender) {
    setIdealBufferingFrames(maxBufferingFrames);
    this->render = videoRender;
  }
  
  void start(const StreamMeta& meta) override {
    addFrameAcceptor(render);
    
    render->start(meta);
    
    auto stream = (AVStream *)meta.data;
    double fps = av_q2d(stream->avg_frame_rate);
    
    lms::dispatchAsyncPeriodically(mainQueue(), fps, [this] {
      if (delegate) {
        dispatchAsync(mainQueue(), [this] {
          delegate->willRunRenderLoop(this);
        });
      }

      bool frameDeliveried = squeezeFrame(0);

      if (delegate) {
        dispatchAsync(mainQueue(), [this, frameDeliveried] () {
          delegate->didRunRenderLoop(this, frameDeliveried);
        });
      }
    });
  }
  
  void stop() override {
    LMSLogError("TODO: Cancel periodic job");
    
    render->stop();

    removeFrameAcceptor(render);
  }
 
private:
  Render *render;
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
    /*
     依赖关系：source -> codec -> renderDriver
                ^        ^        ^
                └──── vstream ────┘
     */
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

class Coordinator : public PacketAcceptor, public DecoderDelegate, public RenderDriverDelegate {
public:
  Coordinator(PassiveMediaSource *source) {
    this->source = lms::retain(source);
  }
  
  ~Coordinator() {
    lms::release(source);
  }

public:
  /* from src */
  void didReceivePacket(Packet *packet) override  {
    packetsLoading  -= 1;
  }
  
  /* from ecoder */
  void willStartDecodingPacket(Packet *packet) override {
    packetsDecoding += 1;
  }
  
  /* from decoder */
  void didFinishDecodingPacket(Packet *packet) override {
    packetsDecoding -= 1;
  }
  
  void didRunRenderLoop(RenderDriver* driver, bool frameRendered) override {
    int packetsToLoad = driver->numberOfEmptySlots() - packetsLoading - packetsDecoding;
    
    // 增加保底缓存帧数，避免当IdealFramesCount过小时（如1），一帧渲染了才去请求下一帧，从而退化为串行处理模式。
    // 应对策略是使用一个保底帧数来尽可能平衡数据加载、解码的速度刚好和渲染的消耗速度相匹配，从而成为并行流水线模式。
    // 如果该调整值过大，会导致缓存的帧数始终大于RenderDriver的理想缓存帧数。
    // 否则，如果该调整值过小，则无法解决退化为穿行处理模式的问题
    const int NumberOfFramesOffsetForLoadingCost = 5;
    packetsToLoad += NumberOfFramesOffsetForLoadingCost;
    
    // 如果一个packet中包含较多的帧，则可能导致framesCached较大。进而使得packetsToLoad为负数
    // 如：初始状态下，发起15个packets加载请求
    // 每个packet中解出了3个frame。则framesCached为45, packetsLoading, packetsDecoding均为0
    // 最终，packetsToLoad 为 15 - 45 - 0 - 0 + 5 = -25
    // 对此，认为只要framesCached超过了FramesExpected，则为缓存充足的情况，可以不发起加载请求
    packetsToLoad = std::max(0, packetsToLoad);

    if (packetsToLoad <= 0) {
      return;
    }

    packetsLoading += packetsToLoad;
    source->loadPackets(packetsToLoad);
  }
  
private:
  PassiveMediaSource *source;
  int packetsLoading = 0;
  int packetsDecoding = 0;
};


Player::Player(PassiveMediaSource *s, Render *vrender) {
  this->source  = lms::retain(s);
  this->vrender = lms::retain(vrender);
  this->vstream = nullptr;
  this->coordinator = lms::retain(new Coordinator(s));
}

Player::~Player() {
  lms::release(coordinator);
  lms::release(vstream);
  lms::release(source);
}

void Player::play() {
  LMSLogInfo(nullptr);

  source->addPacketAcceptor(StreamIdAny, coordinator);

  /* 必须先加载source的数据才能获取当中的元信息 */
  if (source->open() != 0) {
    return;
  }
  
  auto nbStreams = source->numberOfStreams();
  int  streamId = -1;
  for (int i = 0; i < nbStreams; i += 1) {
    auto meta   = source->streamMetaAt(i);
    auto stream = (AVStream *)meta.data;

    if (meta.mediaType == MediaTypeVideo) {
      VideoRenderDriver *driver = autoRelease(new VideoRenderDriver(10, vrender));
      driver->setDelegate(coordinator);

      Decoder *decoder = autoRelease(createDecoder(meta));
      decoder->setDelegate(coordinator);

      vstream = new Stream(meta, source, decoder, nullptr, driver);
    } else if (meta.mediaType == MediaTypeAudio) {
      Decoder *decoder = autoRelease(createDecoder(meta));
      SDLAudioResampler *resampler = autoRelease(new SDLAudioResampler(stream));
      SDLSpeaker *speaker = new SDLSpeaker(stream);
      astream = new Stream(meta, source, decoder, resampler, speaker);
    }
  }
  
  if (vstream == nullptr && astream == nullptr) {
    LMSLogError("Failed creating video & audio stream");
    return;
  }
  
  vstream->start();
  astream->start();

  source->loadPackets(100);
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

  source->removePacketAcceptor(coordinator);

  lms::release(vstream);
  lms::release(coordinator);
}

} // namespace lms
