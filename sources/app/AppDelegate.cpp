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


typedef enum {
  ScaleModeAspectFit = 1,
  ScaleModeAspectFill,
  ScaleModeScaleToFill
} ScaleMode;

SDL_Rect calcDrawRect(ScaleMode mode, int srcWidth, int srcHeight, SDL_Rect bounds) {
  double srcRatio      = (double)srcWidth / (double)srcHeight;
  double boundingRatio = (double)bounds.w / (double)bounds.h;
  
  if (mode == ScaleModeScaleToFill) {
    return bounds;
  }
  
  bool isAspectFit  = (mode == ScaleModeAspectFit);
  bool isAspectFill = (mode == ScaleModeAspectFill);
  SDL_Rect drawRect;
  
  if ((srcRatio < boundingRatio && isAspectFit) || (srcRatio > boundingRatio && isAspectFill)) {
    drawRect.h = bounds.h;
    drawRect.w = srcRatio * drawRect.h;
    drawRect.x = (bounds.w - drawRect.w) / 2 + bounds.x;
    drawRect.y = bounds.y;
  } else {
    drawRect.w = bounds.w;
    drawRect.h = drawRect.w / srcRatio;
    drawRect.y = (bounds.h - drawRect.h) / 2 + bounds.y;
    drawRect.x = bounds.x;
  }
  
  return drawRect;
}

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

class SDLFrameScaler : virtual public lms::Object {
public:
  SDLFrameScaler(int width, int height, AVPixelFormat inputFormat, AVPixelFormat outputFormat, bool reuseFrame = true) {
    this->width        = width;
    this->height       = height;
    this->inputFormat  = inputFormat;
    this->outputFormat = outputFormat;
    
    swsContext = sws_getContext(width, height, inputFormat, width, height, outputFormat, SWS_BILINEAR, NULL, NULL, NULL);
    
    if (reuseFrame) {
      cacheFrame = createFrame();
    }
  }
  
  ~SDLFrameScaler() {
    destroyFrame(cacheFrame);
    sws_freeContext(swsContext);
  }
  
  AVFrame *scale(AVFrame *iframe) {
    assert(iframe->width == width);
    assert(iframe->height == height);
    assert(iframe->format == inputFormat);
    
    AVFrame *oframe = cacheFrame;

    // 如果不进行帧复用，则每一次转换都创建一个新的帧
    if (oframe == nullptr) {
      oframe = createFrame();
    }
    
    sws_scale(swsContext,
              (uint8_t const *const *)iframe->data,
              iframe->linesize,
              0,
              iframe->height,
              oframe->data,
              oframe->linesize);
    
    return  oframe;
  }
  
private:
  AVFrame *createFrame() {
    int bufferSize = av_image_get_buffer_size(outputFormat, width, height, 32);
    uint8_t *buffer = (uint8_t *)av_malloc(bufferSize);
    
    AVFrame *frame = av_frame_alloc();
    av_image_fill_arrays(frame->data, frame->linesize, buffer, AV_PIX_FMT_YUV420P, width, height, 32);

    return frame;
  }
  
  void destroyFrame(AVFrame *frame) {
    if (frame == nullptr) {
      return;
    }

    av_freep(&frame);
  }
  
private:
  SwsContext *swsContext;
  int width;
  int height;
  AVPixelFormat inputFormat;
  AVPixelFormat outputFormat;
  AVFrame *cacheFrame;
};

class SDLView : public lms::Render {
protected:
  void start(const lms::StreamMeta& meta) override {
    this->st = (AVStream *)meta.data;
    auto par = st->codecpar;
    
    Uint32 renderFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE;
    renderer = SDL_CreateRenderer(mainWindow, -1, renderFlags);
    
    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_YV12,
                                SDL_TEXTUREACCESS_STREAMING,
                                par->width,
                                par->height);
    
    // 当输入的帧格式不是YV12时，需要对其进行格式转换，否则无法将yuv数据copy到texture中
    if (par->format != AV_PIX_FMT_YUV420P) {
      scaler = new SDLFrameScaler(par->width, par->height, (AVPixelFormat)par->format, AV_PIX_FMT_YUV420P);
    }
  }
  
  void stop() override {
    lms::release(scaler);
    
    SDL_DestroyTexture(texture);
    texture = nullptr;
    
    SDL_DestroyRenderer(renderer);
    renderer = nullptr;
  }
  
  ScaleMode getContentScaleMode() const {
    return this->scaleMode;
  }
  
  void setContentScaleMode(ScaleMode scaleMode) {
    this->scaleMode = scaleMode;
  }
  
protected:
  
  void didReceiveFrame(lms::Frame *frm) override {
    auto frame = av_frame_clone((AVFrame *)frm);
    
    double ts = frame->best_effort_timestamp * av_q2d(st->time_base);
    LMSLogVerbose("Render video frame | ts:%.2lf, pts:%lld", ts, frame->pts);

    lms::dispatchAsync(lms::mainQueue(), [this, frame] () {
      AVFrame *yuv = scaler ? scaler->scale(frame) : frame;

      SDL_UpdateYUVTexture(texture,
                           NULL,
                           yuv->data[0], yuv->linesize[0],
                           yuv->data[1], yuv->linesize[1],
                           yuv->data[2], yuv->linesize[2]);
      
      av_frame_unref(frame);
      
      int mainWindowWidth, mainWindowHeight;
      SDL_GL_GetDrawableSize(mainWindow, &mainWindowWidth, &mainWindowHeight);
      SDL_Rect bounds = {0, 0, mainWindowWidth, mainWindowHeight};

      SDL_Rect drawRect = calcDrawRect(scaleMode, st->codecpar->width, st->codecpar->height, bounds);

      SDL_RenderClear(renderer);
      SDL_RenderCopy(renderer, texture, NULL, &drawRect);
      SDL_RenderPresent(renderer);
    });
  }
  
private:
  AVStream       *st;
  SDL_Renderer   *renderer  = nullptr;
  SDL_Texture    *texture   = nullptr;
  SDLFrameScaler *scaler    = nullptr;
  ScaleMode       scaleMode = ScaleModeAspectFit;
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

