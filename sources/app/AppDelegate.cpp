#include <lms/Foundation.h>
#include <lms/MediaSource.h>
#include <lms/Player.h>
#include <lms/Decoder.h>
#include <lms/Render.h>
#include <lms/Logger.h>
#include <lms/Buffer.h>
#include <lms/Packet.h>
#include <extension/sdl/Application.h>

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

class FFVideoFile : public lms::PassiveMediaSource {
public:
  FFVideoFile(const char *path) {
    LMSLogVerbose("path: %s", path);

    this->context = nullptr;
    this->path    = strdup(path);
    this->queue   = lms::createDispatchQueue("video_file");
  }

  ~FFVideoFile() {
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
      // TODO: 使用独立的queue来加载数据
      dispatchAsync(lms::mainQueue(), [this] {
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
      });
    }
  }

private:
  char *path;
  AVFormatContext    *context;
  lms::DispatchQueue *queue;
};

class SWSFrameScaler : virtual public lms::Object {
public:
  SWSFrameScaler(int width, int height, AVPixelFormat inputFormat, AVPixelFormat outputFormat, bool reuseFrame = true) {
    this->width        = width;
    this->height       = height;
    this->inputFormat  = inputFormat;
    this->outputFormat = outputFormat;
    
    swsContext = sws_getContext(width, height, inputFormat, width, height, outputFormat, SWS_BILINEAR, NULL, NULL, NULL);
    
    if (reuseFrame) {
      cacheFrame = createFrame();
    }
  }
  
  ~SWSFrameScaler() {
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
    
    // 创建一个SDL窗口用于在其中进行视频渲染
    win = SDL_CreateWindow("LMS Window",
                           SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED,
                           par->width / 2,
                           par->height / 2,
                           SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    
    Uint32 renderFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;

    // 仅当视频帧率小于垂直同步刷新帧率时，开可能开启垂直同步能力。否则会因为刷新率过高，导致SDL_RenderPresent自发阻塞等待
    // 上一次VSync结束。从而导致每一帧的渲染都会有极高的时延。为了避免过于两者过于极致地接近。所以引入了一个误差范围(0.8)
    SDL_DisplayMode displayMode;
    SDL_GetWindowDisplayMode(win, &displayMode);
    double fps = av_q2d(st->avg_frame_rate);
    if (fps < displayMode.refresh_rate * 0.8) {
      renderFlags |= SDL_RENDERER_PRESENTVSYNC;
    }

    renderer = SDL_CreateRenderer(win, -1, renderFlags);
    
    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_YV12,
                                SDL_TEXTUREACCESS_STREAMING,
                                par->width,
                                par->height);
    
    // 当输入的帧格式不是YV12时，需要对其进行格式转换，否则无法将yuv数据copy到texture中
    if (par->format != AV_PIX_FMT_YUV420P) {
      scaler = new SWSFrameScaler(par->width, par->height, (AVPixelFormat)par->format, AV_PIX_FMT_YUV420P);
    }
  }
  
  void stop() override {
    lms::release(scaler);
    
    SDL_DestroyTexture(texture);
    texture = nullptr;
    
    SDL_DestroyRenderer(renderer);
    renderer = nullptr;
    
    SDL_DestroyWindow(win);
    win = nullptr;
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
    
    // 渲染、UI相关的处理只能在主线程调度
    lms::dispatchAsync(lms::mainQueue(), [this, frame] () {
      Uint32 t0 = SDL_GetTicks();

      AVFrame *yuv = scaler ? scaler->scale(frame) : frame;

      SDL_UpdateYUVTexture(texture,
                           NULL,
                           yuv->data[0], yuv->linesize[0],
                           yuv->data[1], yuv->linesize[1],
                           yuv->data[2], yuv->linesize[2]);
            
      Uint32 t1 = SDL_GetTicks();

      int winWidth, winHeight;
      SDL_GL_GetDrawableSize(win, &winWidth, &winHeight);
      SDL_Rect bounds = {0, 0, winWidth, winHeight};

      SDL_Rect drawRect = calcDrawRect(scaleMode, frame->width, frame->height, bounds);

      SDL_RenderClear(renderer);
      SDL_RenderCopy(renderer, texture, NULL, &drawRect);
      SDL_RenderPresent(renderer);

      Uint32 t2 = SDL_GetTicks();

      av_frame_unref(frame);

      LMSLogInfo("Render cost: total=%2u, texture=%2u, present=%2u", t2 - t0, t1 - t0, t2 - t1);
    });
  }
  
private:
  AVStream       *st;
  SDL_Window     *win;
  SDL_Renderer   *renderer  = nullptr;
  SDL_Texture    *texture   = nullptr;
  SWSFrameScaler *scaler    = nullptr;
  ScaleMode       scaleMode = ScaleModeAspectFit;
};

class PlayerAppDelegate: public SDLAppDelegate {
public:  
  void didFinishLaunchingApplication(int argc, char **argv) override {
    lms::init();
    lms::setLogLevel(lms::LogLevelVerbose);

    auto src = lms::autoRelease(new FFVideoFile(argv[1]));
    player = new lms::Player(src, lms::autoRelease(new SDLView));
    player->play();
  }

  void willTerminateApplication() override {
    player->stop();
    lms::release(player);
    lms::unInit();
  }

private:
  lms::Player *player;
};

int main(int argc, char *argv[]) {
  SDLApplication app(argc, argv);
  
  // 通过添加{}来明确delegate的释放时机，避免DumpLeaks误认为delegate为泄露资源
  PlayerAppDelegate delegate;
  app.run(&delegate);
  
  lms::dumpLeaks();
}

