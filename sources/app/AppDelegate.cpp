#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/MediaSource.h"
#include "LMSFoundation/Player.h"
#include "LMSFoundation/Decoder.h"
#include "LMSFoundation/Render.h"
#include "LMSFoundation/Logger.h"
#include "LMSFoundation/Buffer.h"
#include "LMSFoundation/Packet.h"
#include "SDLApplication.h"

extern "C" {
  #include <libavutil/imgutils.h>
  #include <libswscale/swscale.h>
  #include <SDL2/SDL.h>
}

#include <cmath>
#include <inttypes.h>


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
    LMSLogVerbose("loadPackets: requested=%d", numberRequested);
    
    class AVPacketHolder : public lms::ResourceHolder {
    protected:
      void* retain(void *object) override {
        return object;
      }
      
      void release(void *object) override {
        av_packet_unref((AVPacket *)object);
      }
    };
    
    for (int i = 0; i< numberRequested; i += 1) {
      dispatchAsync(lms::mainQueue(), [this] () {
        AVPacket *avpkt = av_packet_alloc();
        int rt = av_read_frame(context, avpkt);
        if (rt >= 0) {
          LMSLogVerbose("AVPacket loaded: st=%d, flags=0x%-2x, dts=%" PRIu64
                        ", pts=%" PRIu64 ", dur=%" PRIu64 ", sz=%-6d",
                        avpkt->stream_index,
                        avpkt->flags,
                        avpkt->dts,
                        avpkt->pts,
                        avpkt->duration,
                        avpkt->size);
  
          
          lms::ResourceHolder *holder = new AVPacketHolder;
          lms::Packet *pkt = new lms::Packet(avpkt, holder);
          pkt->streamIndex = avpkt->stream_index;
          pkt->data        = avpkt->data;
          pkt->size        = avpkt->size;
          pkt->pts         = avpkt->pts;

          deliverPacket(pkt);

          lms::release(pkt);
          lms::release(holder);
        }
      
        return 0;
      });
    }
  }

private:
  char *path;
  AVFormatContext    *context;
  lms::DispatchQueue *queue;
};


class SDLView : public lms::Render {
protected:
  void start(const lms::StreamMeta& meta) override {
    this->st = (AVStream *)meta.data;
    auto par = st->codecpar;
    
    Uint32 renderFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE;
    renderer = SDL_CreateRenderer(mainWindow, -1, renderFlags);
  }
  
  void stop() override {
  }
  
protected:
  void calcImgSize(int &imgHeight, int &imgWidth, int &rectX, int &rectY) {
    double videoRatio  = st->codecpar->width * 1.0 / st->codecpar->height;
    double windowRatio = mainWindowWidth * 1.0 / mainWindowHeight;
    
    if ((videoRatio < windowRatio && aspectFit) || (videoRatio > windowRatio && !aspectFit)) {
      imgHeight = mainWindowHeight;
      imgWidth = videoRatio * imgHeight;
      if (aspectFit){
        rectX = (mainWindowWidth - imgWidth) / 2;
      } else {
        rectX = (imgWidth - mainWindowWidth) / 2;
      }
    } else {
      imgWidth = mainWindowWidth;
      imgHeight = imgWidth / videoRatio;
      if (aspectFit) {
        rectY = (mainWindowHeight - imgHeight) / 2;
      } else {
        rectY = (imgHeight - mainWindowHeight) / 2;
      }
    }
  }
  
  void scaleImg(AVFrame *srcFrame, int srcWidth, int srcHeight, AVPixelFormat srcFormat,
                AVFrame *dstFrame, int dstWidth, int dstHeight, AVPixelFormat dstFormat) {
    sws_ctx = sws_getCachedContext(sws_ctx,
                                   srcWidth,
                                   srcHeight,
                                   srcFormat,
                                   dstWidth,
                                   dstHeight,
                                   dstFormat,
                                   SWS_BILINEAR,
                                   NULL,
                                   NULL,
                                   NULL);
    
    int numBytes = av_image_get_buffer_size(dstFormat,
                                            dstWidth,
                                            dstHeight,
                                            32);
    
    buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    
    av_image_fill_arrays(dstFrame->data, dstFrame->linesize, buffer, dstFormat, dstWidth, dstHeight, 32);
    
    sws_scale(sws_ctx,
              (uint8_t const *const *)srcFrame->data,
              srcFrame->linesize,
              0,
              srcHeight,
              dstFrame->data,
              dstFrame->linesize);
  }
  
  void didReceiveFrame(lms::Frame *frm) override {
    
    auto frame = (AVFrame *)frm;
    
    double ts = frame->best_effort_timestamp * av_q2d(st->time_base);
    LMSLogVerbose("Render video frame | ts:%.2lf, pts:%lld", ts, frame->pts);
    
    SDL_DestroyTexture(texture);
    
    int imgWidth, imgHeight;
    int rectX = 0;
    int rectY = 0;
        
    calcImgSize(imgHeight, imgWidth, rectX, rectY);
    
    AVFrame *yuv = av_frame_alloc();
    scaleImg(frame, st->codecpar->width, st->codecpar->height, (AVPixelFormat)st->codecpar->format,
             yuv, imgWidth, imgHeight, AV_PIX_FMT_YUV420P);
       
    rect.x = 0;
    rect.y = 0;
    rect.w = imgWidth;
    rect.h = imgHeight;
    
    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_YV12,
                                SDL_TEXTUREACCESS_STREAMING,
                                imgWidth,
                                imgHeight);

    SDL_UpdateYUVTexture(texture,
                         &rect,
                         yuv->data[0], yuv->linesize[0],
                         yuv->data[1], yuv->linesize[1],
                         yuv->data[2], yuv->linesize[2]);
    
    av_frame_free(&yuv);

    SDL_RenderClear(renderer);
    if (aspectFit) {
      SDL_Rect dstRect = {rectX, rectY, imgWidth, imgHeight};
      SDL_RenderCopy(renderer, texture, NULL, &dstRect);
    } else {
      SDL_Rect srcRect = {rectX, rectY, mainWindowWidth, mainWindowHeight};
      SDL_RenderCopy(renderer, texture, &srcRect, NULL);
    }
    SDL_RenderPresent(renderer);
  }
  
private:
  AVStream *st;
  struct SwsContext *sws_ctx  = nullptr;
  uint8_t           *buffer   = nullptr;
  SDL_Renderer      *renderer = nullptr;
  SDL_Texture       *texture  = nullptr;
  SDL_Rect           rect;
  bool              aspectFit = true;
};

struct FPSTimer {
  bool           shouldQuit;
  double         period;
  lms::Runnable *runnable;
  
  FPSTimer(double period, lms::Runnable *r) {
    this->shouldQuit = false;
    this->period     = period;
    this->runnable   = lms::retain(r);
  }
  
  ~FPSTimer() {
    lms::release(runnable);
  }
};

static int fpsTimerFunc(FPSTimer *timer) {
  const double delayPerFrame = 1.0 / timer->period * 1000;
  const Uint32 preDelay = (Uint32)(delayPerFrame * 0.2);
  Uint32 begin  = SDL_GetTicks();
  LMSLogDebug("begin: %u, delayPerFrame: %lf", begin, delayPerFrame);
  
  while(!timer->shouldQuit) {
    LMSLogVerbose("FPS timer callback on: %u", SDL_GetTicks());

    lms::dispatchAsync(lms::mainQueue(), timer->runnable);
//    timer->runnable->run();
    
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

  delete timer;
}

static FPSTimer *__timer;
static SDL_Thread* __timerThread;

static VideoFile *src;

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
  
  int asyncPeriodically(double period, lms::Runnable *runnable) override {
    assert(__timerThread == nullptr);
    __timer = new FPSTimer(period, runnable);
    __timerThread = SDL_CreateThread((SDL_ThreadFunction)fpsTimerFunc, "FPSTimer", __timer);
  }
  
  void cancelPeriodicalJob(int jobId) override {
    __timer->shouldQuit = true;
    SDL_WaitThread(__timerThread, nullptr);
  }

private:
  lms::Player *player;
};


int main(int argc, char *argv[]) {
  SDLApplication app(argc, argv);
  
  // 通过添加{}来明确delegate的释放时机，避免DumpLeaks误认为delegate为泄露资源
  {
    PlayerAppDelegate delegate;
    app.run(&delegate);
  }
  
  lms::dumpLeaks();
}

