#include <stdio.h>
#include <set>
#include <list>
#include <memory>
#include <vector>
#include <algorithm>

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

static volatile int64_t relative_zero_ticks = -1;

void dumpBytes(uint8_t *data, int size, int bytesPerLine) {
  for (int i = 0; i < size; ++i) {
    printf("%02X ", data[i]);
    if ((i + 1) % bytesPerLine == 0) {
      printf("\n");
    }
  }

  if (size % bytesPerLine != 0) {
    printf("\n");
  }
}

namespace mc {

class packet;
class stream_t;
class frame_t;

class device {
  virtual int start() = 0;
  virtual void stop() = 0;
};

class packet_acceptor {
public:
  virtual void on_receive_packet(packet *pkt) = 0;
};

class packet_output {
public:
  void set_acceptor(packet_acceptor *acceptor) {
    this->acceptor = acceptor;
  }

protected:
  packet_acceptor *acceptor;
};

class packet_inout: public packet_acceptor, public packet_output {};

typedef int (*action_pf)(void *context, void *user_data);

class action_dispatcher {
public:
  virtual void dispatch(action_pf action, void *context, void *user_data) = 0;
};

class action_runloop {
public:
  virtual void run(action_pf action, void *context, void *user_data) = 0;
  virtual void stop(action_pf act, void *context) = 0;
};

class direct_driver: public action_dispatcher {
public:
  void dispatch(action_pf act, void *context, void *user_data) override {
    act(context, user_data);
  }
  
  static direct_driver instance;
};

class sdl_direct_runloop: public action_runloop {
public:
  static void launch() {
    SDL_Event event;
    for (;;) {
      SDL_WaitEvent(&event);
      switch(event.type) {
        case SDL_QUIT:
          SDL_Quit();
          exit(0);
          break;
      }
      
      auto cpy = actions;
      std::for_each(cpy.begin(), cpy.end(), [] (const std::tuple<action_pf, void *, void *>& item) {
        auto& act = std::get<0>(item);
        act(std::get<1>(item), std::get<2>(item));
      });
    }
  }
  
protected:
  void run(action_pf act, void *context, void *user_data) override {
    actions.push_back(std::make_tuple(act, context, user_data));
  }

  void stop(action_pf act, void *context) override {
    actions.remove_if([act, context] (std::tuple<action_pf, void *, void *>& item) {
      return std::get<0>(item) == act && std::get<1>(item) == context;
    });
  }
  
public:
  static sdl_direct_runloop instance;
  static std::list<std::tuple<action_pf, void *, void *> > actions;
};

std::list<std::tuple<action_pf, void *, void *> > sdl_direct_runloop::actions;

sdl_direct_runloop sdl_direct_runloop::instance;
direct_driver direct_driver::instance;

class sdl_thread_runloop: public action_runloop {
public:
  sdl_thread_runloop() {
    is_running = false;
  }

public:
  void run(action_pf action, void *user_context, void *user_data) override {
    if (is_running) {
      return;
    }
    
    runloop_context_t * context = (runloop_context_t *) malloc(sizeof(runloop_context_t));
    context->self         = this;
    context->action       = action;
    context->user_context = user_context;
    context->user_data    = user_data;

    printf("sdl_thread_runloop::run()\n");

    is_running = true;
    thread = SDL_CreateThread((SDL_ThreadFunction)thread_function, "sdl_thread_runloop", context);
  }
  
  void stop(action_pf act, void *context) override {
    printf("sdl_thread_runloop::stop()\n");
    is_running = false;
    
    int status = 0;
    SDL_WaitThread(thread, &status);
  }
  
private:
  struct runloop_context_t {
    sdl_thread_runloop *self;
    action_pf action;
    void *user_context;
    void *user_data;
  };

  static int thread_function(runloop_context_t *info) {
    while(info->self->is_running && info->action(info->user_context, info->user_data) == 0);
    return 0;
  }
          
private:
  volatile bool is_running;
  SDL_Thread *thread;
};

class driver_center {
public:
  static action_dispatcher *create_dispatcher(const char *name) {
    return &direct_driver::instance;
  }
  
  static action_runloop *create_runloop(const char *name) {
    if (strcmp(name, "sdl") == 0) {
      return new sdl_thread_runloop();
    } else {
      return &sdl_direct_runloop::instance;
    }
  }
  
  static void destroy_dispatcher(action_dispatcher *dispatcher) {
  }
  
  static void destroy_runloop(action_runloop *wheeler) {
  }
};

class frame_acceptor {
public:
  virtual void on_receive_frame(frame_t *frame) = 0;
};

class frame_output {
public:
  frame_output() {
    this->acceptor = nullptr;
  }
  
  void set_acceptor(frame_acceptor *acceptor) {
    this->acceptor = acceptor;
  }
  
protected:
  frame_acceptor *acceptor;
};

class frame_inout: public frame_acceptor, public frame_output {};

class frame_fork: public frame_acceptor {
public:
  void add_acceptor(frame_acceptor *acceptor) {
    acceptors.insert(acceptor);
  }
  
  void remove_acceptor(frame_acceptor *acceptor) {
    acceptors.erase(acceptor);
  }
  
protected:
  void on_receive_frame(frame_t *frame) override {
    std::for_each(acceptors.begin(), acceptors.end(), [frame] (frame_acceptor *ac) {
      ac->on_receive_frame(frame);
    });
  }

private:
  std::set<frame_acceptor *> acceptors;
};

class decoder: public packet_acceptor, public frame_output {};

class ff_decoder: public decoder {
public:
  ff_decoder(AVStream *stream) {
    this->stream = stream;
    this->dispatcher = driver_center::create_dispatcher("decoder");
    
    AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    context = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(context, stream->codecpar);
    context->pkt_timebase = stream->time_base;
    avcodec_open2(context, codec, 0);

    frame_decoded = av_frame_alloc();
  }
  
  ~ff_decoder() {
    avcodec_close(context);
    avcodec_free_context(&context);
    
    driver_center::destroy_dispatcher(dispatcher);
  }
  
  AVCodecContext* getContext() {
    return context;
  }
  
protected:
  void on_receive_packet(packet *pkt) {
    decode_packet((AVPacket *)pkt);
  }
  
private:
  void decode_packet(AVPacket *packet) {
    int response = avcodec_send_packet(context, packet);
    AVFrame *frame = frame_decoded;
    
    while(response >= 0) {
      response = avcodec_receive_frame(context, frame);
      
      if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
        break;
      }
      
      if (response != 0) {
        continue;
      }
      
      static int i = 0;
      
      if (packet->stream_index == 1 && i < 10) {
        printf("Frame:[%lld] size:%d\n", frame->pts, frame->linesize[0]);
        dumpBytes(frame->data[0], frame->linesize[0], 48);
        i += 1;
      }
      
      frame->pts = guess_correct_pts(frame);// frame->pts * context->time_base.num / context->time_base.den * context->ticks_per_frame;
      frame->display_picture_number = i++;

      if (acceptor != nullptr) {
        acceptor->on_receive_frame((frame_t *)frame);
      }

      av_frame_unref(frame);
    }
  }
  
  int64_t guess_correct_pts(AVFrame *frame) {
    int64_t dts = frame->pkt_dts;
    int64_t reordered_pts = frame->reordered_opaque;
    int64_t pts = AV_NOPTS_VALUE;

    if (dts != AV_NOPTS_VALUE) {
      context->pts_correction_num_faulty_dts += (dts <= context->pts_correction_last_dts);
      context->pts_correction_last_dts = dts;
    } else if (reordered_pts != AV_NOPTS_VALUE) {
      context->pts_correction_last_dts = reordered_pts;
    }

    if (reordered_pts != AV_NOPTS_VALUE) {
      context->pts_correction_num_faulty_pts += (reordered_pts <= context->pts_correction_last_pts);
      context->pts_correction_last_pts = reordered_pts;
    } else if (dts != AV_NOPTS_VALUE) {
      context->pts_correction_last_pts = dts;
    }

    if ((context->pts_correction_num_faulty_pts <= context->pts_correction_num_faulty_dts || dts == AV_NOPTS_VALUE) && reordered_pts != AV_NOPTS_VALUE) {
        pts = reordered_pts;
    } else {
        pts = dts;
    }

    return pts;
  }

private:
  AVStream *stream;
  action_dispatcher *dispatcher;
  AVCodecContext *context;
  AVFrame *frame_decoded;
};

class ff_avfloader : public device, public packet_output {
public:
  ff_avfloader(const char *file_path) {
    skip = false;
    runloop = driver_center::create_runloop("sdl");

    context = nullptr;
    avformat_open_input(&context, file_path, NULL, NULL);
    avformat_find_stream_info(context, NULL);
    
    av_dump_format(context, 0, file_path, 0);
  }
  
  ~ff_avfloader() {
    driver_center::destroy_runloop(runloop);
    avformat_close_input(&context);
  }
  
  std::vector<AVStream *> streams() const {
    return std::vector<AVStream *>(context->streams, context->streams + context->nb_streams);
  }
  
  AVStream *first_stream(AVMediaType codec_type) {
    for (int i = 0; i < context->nb_streams; ++i) {
      if (context->streams[i]->codecpar->codec_type == codec_type) {
        return context->streams[i];
      }
    }
    return nullptr;
  }
  
  void setSkip(bool skip) {
    printf("ff_avfloader::setSkip(%s)\n", skip ? "true" : "false");
    this->skip = skip;
  }
  
public:
  int start() override {
    runloop->run((action_pf)load_packets, this, nullptr);
    return 0;
  }
  
  void stop() override {
    runloop->stop((action_pf)load_packets, this);
  }
  
private:
  static int load_packets(ff_avfloader *self, void *user_data) {
    if (self->skip) {
      return 0;
    }
    
    int status = av_read_frame(self->context, &self->pkt);
    if (self->acceptor != nullptr) {
      self->acceptor->on_receive_packet((packet *)&self->pkt);
    }
    
    if (status >= 0) {
      av_packet_unref(&self->pkt);
    }
    return status;
  }
  
public:
  AVFormatContext *context;
  AVPacket pkt;
  action_runloop *runloop;
  
  volatile bool skip;
};

class ff_packet_dumper: public packet_inout {
protected:
  void on_receive_packet(packet *pkt) override {
    AVPacket *avpkt = (AVPacket *)pkt;
    printf("Packet {stream:%d, size:%d, pts:%lld, dts:%lld}\n"
           , avpkt->stream_index
           , avpkt->size
           , avpkt->pts
           , avpkt->dts);
    
    if (this->acceptor != nullptr) {
      this->acceptor->on_receive_packet(pkt);
    }
  }
};

class ff_frame_dumper: public frame_inout {
public:
  ff_frame_dumper(int frame_type) {
    this->type = frame_type;
  }

protected:
  void on_receive_frame(frame_t *frm) override {
    AVFrame *frame = (AVFrame *)frm;
    
    int size = 0;
    for (int i = 0; i < 1; ++i) {
      size += frame->linesize[i];
    }
    
    if (type == 0) {
      dump_video_frame(frame, size);
    } else if (type == 1) {
      dump_audio_frame(frame, size);
    }
   
    if (this->acceptor != nullptr) {
      this->acceptor->on_receive_frame(frm);
    }
  }
  
  static void dump_audio_frame(AVFrame *frame, int size) {
//    printf("Frame %-5d {fmt:%-5d, size:%-6d, samples:%-4d, pts:%-10lld}\n"
//           , frame->display_picture_number
//           , frame->format
//           , size
//           , frame->nb_samples
//           , frame->pts
//           );
//    static int index = 0;
//
//    if (index < 10) {
//      printf("Frame[%lld]\n", frame->pts);
//      dumpBytes((uint8_t *)frame->data, frame->linesize[0], 48);
//      index += 1;
//    }
  }
  
  static void dump_video_frame(AVFrame *frame, int size) {
    printf("Frame %-5d (%c, size=%6d bytes, format=%d) pts:%lld\n",
        frame->display_picture_number,
        av_get_picture_type_char(frame->pict_type),
        size,
        frame->format,
        frame->pts);
  }

private:
  int type; /* 0: video, 1: audio */
};

class ff_packets_demuxer: public packet_acceptor {
public:
  void add_acceptor(packet_acceptor *acceptor, int stream_index) {
    acceptors.push_back(std::make_pair(acceptor, stream_index));
  }
  
  void remove_acceptor(packet_acceptor *acceptor, int stream_index) {
    auto it = std::remove_if(acceptors.begin(),
                   acceptors.end(),
                   [acceptor, stream_index] (std::pair<packet_acceptor *, int>& item) {
      return stream_index == -1 || (item.first == acceptor && item.second == stream_index);
    });
    
    acceptors.erase(it, acceptors.end());
  }

public:
  void on_receive_packet(packet *pkt) {
    AVPacket *avpkt = (AVPacket *)pkt;
    std::for_each(acceptors.begin(), acceptors.end(), [avpkt](std::pair<packet_acceptor *, int>& item) {
      if (item.second == -1 || avpkt->stream_index == item.second) {
        item.first->on_receive_packet((packet *)avpkt);
      }
    });
  }

private:
  std::list<std::pair<packet_acceptor *, int> > acceptors;
};

class sdl_window_delegate {
public:
  virtual void on_frames_changed(size_t size) = 0;
};

class sdl_window_renderer: public device, public frame_acceptor {
public:
  sdl_window_renderer(AVStream *stream) {
    this->stream = stream;
    this->delegate = nullptr;
    this->runloop = driver_center::create_runloop("window");
    
    this->mutex = SDL_CreateMutex();
    this->cond  = SDL_CreateCond();

    // 创建一个SDL窗口用于在其中进行视频渲染
    SDL_Window *win = SDL_CreateWindow("FFmpeg Labs",
                                       SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED,
                                       stream->codecpar->width / 2,
                                       stream->codecpar->height / 2,
                                       SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI);

    // 有了用于呈现视频的窗口，还需要创建一个可以将图像渲染到窗口的渲染器
    renderer = SDL_CreateRenderer(win,
                                  -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE
                                  );
    
    // 渲染器在进行视频渲染时，需要一个纹理对象承载待渲染的数据
    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_YV12,
                                SDL_TEXTUREACCESS_STREAMING,
                                stream->codecpar->width,
                                stream->codecpar->height);
  }
  
public:
  void on_receive_frame(frame_t *frm) override {
    push_frame((AVFrame *)frm, false, true);
  }
  
  int start() override {
    runloop->run((action_pf)render, this, nullptr);
    return 0;
  }
  
  void stop() override {
    runloop->stop((action_pf)render, this);
  }
  
private:
  static int render(sdl_window_renderer *self, void *user_data) {
    if (relative_zero_ticks < 0) {
      return 0;
    }
    
    AVFrame *frame = self->pop_frame();
    if (!frame) {
      return 0;
    }

    double avg_frame_duration = 1.0 / av_q2d(self->stream->avg_frame_rate);
    
    int64_t player_ticks = SDL_GetTicks() - relative_zero_ticks;
    int64_t diff = player_ticks - frame->pts * av_q2d(self->stream->time_base) * 1000;
    if (diff >= 0) {
      double offset = (double)diff / 1000.0 / avg_frame_duration;
      printf("render(%-3d) %lld - %lld  %lld ~ %lf%%\n", frame->display_picture_number, player_ticks, frame->pts, diff, offset * 100);
      
      if (offset < 0.4) {
        self->render_frame(frame);
      }
      
      av_frame_unref(frame);
      av_frame_unref(frame);
      av_frame_free(&frame);
      
      if (self->delegate) {
        self->delegate->on_frames_changed(self->frames.size());
      }
    } else {
      self->push_frame(frame, true, false);
    }

    return 0;
  }
  
  AVFrame *pop_frame() {
    AVFrame *frame = nullptr;

    SDL_LockMutex(mutex);
    {
      if (frames.size() > 0) {
        frame = frames.front();
        frames.pop_front();
      }
    }
    SDL_UnlockMutex(mutex);
    
    return frame;
  }
  
  void push_frame(AVFrame *src_frame, bool in_front, bool clone) {
    AVFrame *frame = src_frame;
    if (clone) {
      frame = av_frame_clone(src_frame);
    }
    
    SDL_LockMutex(mutex);
    {
      if (in_front) {
        frames.push_front(frame);
      } else {
        frames.push_back(frame);
      }

      SDL_CondSignal(cond);
    }
    SDL_UnlockMutex(mutex);
  }
  
  void render_frame(AVFrame *frame) {
    SDL_Rect rect = {0, 0, frame->width, frame->height};
    
    SDL_UpdateYUVTexture(texture,
                         &rect,
                         frame->data[0], frame->linesize[0], // Component Y
                         frame->data[1], frame->linesize[1], // Component U
                         frame->data[2], frame->linesize[2]  // Component V
                         );

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
  }

public:
  AVStream *stream;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  action_runloop *runloop;

  std::list<AVFrame *> frames;
  
  SDL_mutex *mutex;
  SDL_cond *cond;
  sdl_window_delegate* delegate;
};

class ff_audio_resampler: public frame_inout {
  AVStream *stream;
  int out_channel_layout;
  int out_sample_rate;
  AVSampleFormat out_sample_format;

public:
  ff_audio_resampler(AVStream *stream, int out_channel_layout, int out_sample_rate) {
    this->stream = stream;
    this->out_sample_format  = AV_SAMPLE_FMT_S16;
    this->out_sample_rate    = out_sample_rate;
    this->out_channel_layout = out_channel_layout;
  }
  
public:
  void on_receive_frame(frame_t *frame) override {
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
//    printf("resampled_size:%d\n", resampled_data_size);
    
    av_freep(&resampled_data[0]);
    av_freep(&resampled_data);
    swr_free(&context);
    
    if (acceptor) {
      acceptor->on_receive_frame((frame_t *)frame_resampled);
    }
  }
};

class sdl_speaker: public frame_acceptor {
public:
  sdl_speaker(uint64_t channel_layout, int sample_rate) {
    this->mutex = SDL_CreateMutex();
    this->cond  = SDL_CreateCond();
    
    SDL_AudioSpec request_specs, respond_specs;
    request_specs.freq     = sample_rate;
    request_specs.format   = AUDIO_S16;
    request_specs.channels = av_get_channel_layout_nb_channels(channel_layout);
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
  void on_receive_frame(frame_t *frame) override {
    SDL_LockMutex(this->mutex);
    {
      frames.push_back((AVFrame *)frame);
      SDL_CondSignal(this->cond);
    }
    SDL_UnlockMutex(this->mutex);
  }
  
private:
  static void load_audio_data(sdl_speaker *self, Uint8 *data, int len) {
    if (relative_zero_ticks < 0) {
      relative_zero_ticks = SDL_GetTicks();
    }
    
    memset(data, 0, len);

    while(len > 0) {
      AVFrame *frame = self->pop_frame();
      if (frame->linesize[0] < len) {
        continue;
      }
      
      int bytes_to_write = std::min(frame->linesize[0], len);
      memcpy(data, frame->opaque, bytes_to_write);
      
      static int index = 0;
      
//      printf("[%d] len:%d, render audio: frame(%d) data[0]:%p, opaque:%p, linesize[0]:%d, btw:%d\n"
//             , index++
//             , len
//             , frame->display_picture_number
//             , frame->data[0]
//             , frame->opaque
//             , frame->linesize[0]
//             , bytes_to_write);
//      for (int i = 0; i < bytes_to_write; ++i) {
//        printf("%02X ", data[i]);
//        if ((i + 1) % 48 == 0) {
//          printf("\n");
//        }
//      }
//      printf("\n");
      
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
        break;
      } else {
        // unlock mutex and wait for cond signal, then lock mutex again
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
};

void link(frame_output* output, frame_acceptor* acceptor) {
  output->set_acceptor(acceptor);
}

void link(frame_fork *fork, frame_acceptor* acceptor) {
  fork->add_acceptor(acceptor);
}

void link(packet_output* output, packet_acceptor* acceptor) {
  output->set_acceptor(acceptor);
}

void link(ff_packets_demuxer* demuxer, int stream_idx, packet_acceptor *acceptor) {
  demuxer->add_acceptor(acceptor, stream_idx);
}

inline void operator >> (frame_output& out, frame_acceptor& acc) { link(&out, &acc); }
inline void operator >> (frame_fork& fork, frame_acceptor& acc) { link(&fork, &acc); }
inline frame_output& operator >> (frame_output& out, frame_inout& inout) { link(&out, &inout); return inout; }
inline frame_fork& operator >> (frame_output& out, frame_fork& fork) { link(&out, &fork); return fork; }
inline frame_output& operator >> (frame_fork& fork, frame_inout& inout) { link(&fork, &inout); return inout; }

inline void operator >> (packet_output& out, packet_acceptor& acc) { link(&out, &acc); }
inline void operator >> (const std::pair<ff_packets_demuxer*, int>& pin_out, packet_acceptor& acc) { link(pin_out.first, pin_out.second, &acc); }
inline packet_output& operator >> (packet_output& out, packet_inout& inout) { link(&out, &inout); return inout; }
inline packet_output& operator >> (const std::pair<ff_packets_demuxer*, int>& pin_out, packet_inout& inout) { link(pin_out.first, pin_out.second, &inout); return inout; }

inline frame_output& operator >> (packet_output& out, decoder& dec) { link(&out, &dec); return dec; }
inline frame_output& operator >> (const std::pair<ff_packets_demuxer*, int>& pin_out, decoder& dec) { link(pin_out.first, pin_out.second, &dec); return dec; }

}

using namespace mc;

class qos: public frame_inout, mc::sdl_window_delegate {
public:
  qos(ff_avfloader& loader, sdl_window_renderer& win): loader(loader), renderer(win) {
    renderer.delegate = this;
  }
  
public:
  void on_receive_frame(frame_t *frame) override {
    if (acceptor) {
      acceptor->on_receive_frame(frame);
    }
    
    if (renderer.frames.size() > 10) {
      loader.setSkip(true);
    }
  }
  
  void on_frames_changed(size_t size) override {
    if (size < 10) {
      loader.setSkip(false);
    }
  }

private:
  ff_avfloader& loader;
  sdl_window_renderer& renderer;
};

int main(int argc, char *argv[]) {
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Can't initialize SDL: %s\n", SDL_GetError());
    exit(1);
  }

  const char *path = argv[1];
  ff_avfloader loader(path);
  ff_packets_demuxer demuxer;
  ff_packet_dumper pkt_dumper;
  ff_frame_dumper vfrm_dumper(0);
  ff_frame_dumper afrm_dumper_1(1);
  ff_frame_dumper afrm_dumper_2(1);

  AVStream *video_stream = loader.first_stream(AVMEDIA_TYPE_VIDEO);
  ff_decoder video_decoder(video_stream);
  sdl_window_renderer win(video_stream);
  qos q(loader, win);
  
  AVStream *audio_stream = loader.first_stream(AVMEDIA_TYPE_AUDIO);
  ff_decoder audio_decoder(audio_stream);
  ff_audio_resampler aresampler(audio_stream, audio_stream->codecpar->channel_layout, audio_stream->codecpar->sample_rate);
  sdl_speaker speaker(audio_stream->codecpar->channel_layout, audio_stream->codecpar->sample_rate);
  
  loader >> demuxer;
//  std::make_pair(&demuxer, video_stream->index) >> video_decoder >> win;
  std::make_pair(&demuxer, audio_stream->index) >> audio_decoder >> aresampler >> speaker;

  loader.start();
  win.start();
  
  sdl_direct_runloop::launch();
  
  return 0;
}
