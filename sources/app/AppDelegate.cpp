#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/MediaSource.h"
#include "LMSFoundation/Player.h"
#include "LMSFoundation/Decoder.h"
#include "LMSFoundation/Render.h"
#include "LMSFoundation/Logger.h"
#include "LMSFoundation/Buffer.h"
#include "SDLApplication.h"

extern "C" {
  #include <libavutil/imgutils.h>
  #include <libswscale/swscale.h>
  #include <SDL2/SDL.h>
}

#include <cmath>


class VideoFile : public lms::PassiveMediaSource {
public:
  VideoFile(const char *path) {
    LMSLogVerbose("path: %s", path);

    this->context = nullptr;
    this->path    = strdup(path);
    this->queue   = lms::createDispatchQueue("video_file");
  }

  ~VideoFile() {
    assert(context == nullptr);

    lms::release(this->queue);
    free(this->path);
  }

  int numberOfStreams() override {
    return context->nb_streams;
  }
  
  lms::StreamMeta streamMetaAt(int index) override {
    auto st = context->streams[index];
    return {
      "ffmpeg",
      st,
      index,
      (lms::MediaType)st->codecpar->codec_type,
      av_q2d(st->avg_frame_rate),
    };
  }

  int open() override {
    int rt = 0;

    rt = avformat_open_input(&context, path, nullptr, nullptr);
    if (rt != 0) {
      LMSLogError("Failed opening video file: %s", path);
      return rt;
    }

    rt = avformat_find_stream_info(context, nullptr);
    if (rt != 0) {
      LMSLogError("Failed finding stream info");
      return rt;
    }
    
    av_dump_format(context, 0, path, 0);
    return 0;
  }

  void close() override {
    avformat_close_input(&context);
  }

  void loadPackets(int numberRequested) override {
    LMSLogVerbose("numberRequested: %d", numberRequested);
    
    dispatchAsync(lms::mainQueue(), [this, numberRequested] () {
      int numberRemains = numberRequested;
      while(numberRemains > 0) {
        AVPacket *packet = av_packet_alloc();
            
        int rt = av_read_frame(context, packet);
        if (rt >= 0) {
          deliverPacket(packet);
        }
        
        numberRemains -= 1;
      }
      
      return 0;
    });
  }

private:
  char *path;
  AVFormatContext    *context;
  lms::DispatchQueue *queue;
};


class SDLView : public lms::Render {
protected:
  void start(const lms::DecoderMeta& meta) override {
    cc = (AVCodecContext *)meta.data;
    
    Uint32 renderFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE;
    renderer = SDL_CreateRenderer(mainWindow, -1, renderFlags);
    
    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_YV12,
                                SDL_TEXTUREACCESS_STREAMING,
                                cc->width,
                                cc->height);
    
    sws_ctx = sws_getContext(cc->width,
                             cc->height,
                             cc->pix_fmt,
                             cc->width,
                             cc->height,
                             AV_PIX_FMT_YUV420P,
                             SWS_BILINEAR,
                             NULL,
                             NULL,
                             NULL);
    
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
                                            cc->width,
                                            cc->height,
                                            32);
    
    buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    
    yuv = av_frame_alloc();
    av_image_fill_arrays(yuv->data, yuv->linesize, buffer, AV_PIX_FMT_YUV420P, cc->width, cc->height, 32);
  }
  
  void stop() override {
  }
  
protected:
  void didReceiveFrame(lms::Frame *frm) override {
    auto frame = (AVFrame *)frm;
    
    LMSLogVerbose("Start rendering frame: %p", frame);
    rect.x = 0;
    rect.y = 0;
    rect.w = cc->width;
    rect.h = cc->height;

    sws_scale(sws_ctx,
              (uint8_t const *const *)frame->data,
              frame->linesize,
              0,
              cc->height,
              yuv->data,
              yuv->linesize);

    SDL_UpdateYUVTexture(texture,
                         &rect,
                         yuv->data[0], yuv->linesize[0],
                         yuv->data[1], yuv->linesize[1],
                         yuv->data[2], yuv->linesize[2]);

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
  }
  
private:
  AVCodecContext    *cc;
  struct SwsContext *sws_ctx  = nullptr;
  uint8_t           *buffer   = nullptr;
  AVFrame           *yuv      = nullptr;
  SDL_Renderer      *renderer = nullptr;
  SDL_Texture       *texture  = nullptr;
  SDL_Rect           rect;
};

typedef struct {
  bool           shouldQuit;
  float          fps;
  lms::Runnable *runnable;
} FPSTimer;

static int fpsTimerFunc(FPSTimer *timer) {
  const double delayPerFrame = 1.0 / timer->fps * 1000;
  const Uint32 preDelay = (Uint32)(delayPerFrame * 0.2);
  Uint32 begin  = SDL_GetTicks();
  LMSLogDebug("begin: %u, delayPerFrame: %lf", begin, delayPerFrame);
  
  while(!timer->shouldQuit) {
    LMSLogVerbose("FPS timer callback on: %u", SDL_GetTicks());

    timer->runnable->run();
    
    // 避免 runnable 执行过快（小于1ms)，导致下面的delay计算结果为0
    // 从而进入短暂的忙循环
    SDL_Delay(preDelay);

    Uint32 now = SDL_GetTicks();
    double diff = (double)(now - begin) / delayPerFrame;
    int delay = (int)((1.0 - diff + std::floor(diff)) * delayPerFrame);

    Uint32 expectedBreakPoint = SDL_GetTicks() + delay;
    do {
      delay = expectedBreakPoint - SDL_GetTicks();
      
      // 如果使用最小睡眠单位1ms不断重复，仍然会带来一定的CPU负载
      // 所以如果预期时间较长时，可以睡眠相对长的时间段，减轻这种负载
      if (delay > 10) {
        SDL_Delay(delay / 2);
      } else if (delay >= 5) {
        SDL_Delay(3);
      } else if (delay >= 1) {
        SDL_Delay(1);
      }
    } while(SDL_GetTicks() < expectedBreakPoint);
  }
}

class PlayerAppDelegate: public SDLAppDelegate, public lms::DispatchQueue {
public:
  void didFinishLaunchingApplication(int argc, char **argv) override {
    lms::init({this});
    lms::setLogLevel(lms::LogLevelVerbose);

    auto src = lms::autoRelease(new VideoFile(argv[1]));
    player = new lms::Player(src, lms::autoRelease(new SDLView));
    player->play();
  }

  void willTerminateApplication() override {
    player->stop();
    lms::release(player);

    lms::unInit();
  }

public:
  void async(lms::Runnable *runnable) override {
    SDL_DispatchRunnable(runnable);
  }
  
  int asyncPeriodically(int delay, lms::Runnable *runnable) override {
    lms::retain(runnable);
    SDL_CreateThread((SDL_ThreadFunction)fpsTimerFunc, "FPSTimer", new FPSTimer{false, 29.7, runnable});
  }

private:
  lms::Player *player;
};


int main(int argc, char *argv[]) {
  SDLApplication app(argc, argv);
  PlayerAppDelegate delegate;
  app.run(&delegate);
}

