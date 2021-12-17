#include "SDLView.h"
#include <lms/MediaSource.h>
#include <lms/Logger.h>
#include <lms/Runtime.h>
#include <lms/Foundation.h>
extern "C" {
  #include <libavformat/avformat.h>
  #include <libavutil/imgutils.h>
  #include <libswscale/swscale.h>
  #include <SDL2/SDL.h>
}

static SDL_Rect calcDrawRect(SDLView::ContentMode mode, int srcWidth, int srcHeight, SDL_Rect bounds) {
  double srcRatio      = (double)srcWidth / (double)srcHeight;
  double boundingRatio = (double)bounds.w / (double)bounds.h;
  
  if (mode == SDLView::scaleToFill) {
    return bounds;
  }
  
  bool isAspectFit  = (mode == SDLView::aspectFit);
  bool isAspectFill = (mode == SDLView::aspectFill);
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

void SDLView::configure(const lms::StreamMetaInfo &meta) {
  this->st = (AVStream *)(meta.at("stream_object").value.ptr);
}

void SDLView::start() {
  LMSLogDebug("SDLView=%p", this);
  
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

void SDLView::stop() {
  LMSLogDebug("SDLView=%p", this);

  lms::mainQueue()->cancel(this);
  lms::release(scaler);
  
  SDL_DestroyTexture(texture);
  texture = nullptr;
  
  SDL_DestroyRenderer(renderer);
  renderer = nullptr;
  
  SDL_DestroyWindow(win);
  win = nullptr;
}

SDLView::ContentMode SDLView::getContentMode() const {
  return this->contentMode;
}

void SDLView::setContentMode(SDLView::ContentMode contentMode) {
  this->contentMode = contentMode;
}

void SDLView::didReceiveCellMessage(const lms::CellMessage &msg) {
  AVFrame *inputFrame = (AVFrame *)msg.at("media-frame").value.ptr;
  auto frame = av_frame_clone(inputFrame);
  
  double ts = frame->best_effort_timestamp * av_q2d(st->time_base);
  LMSLogVerbose("Render video frame | ts:%.2lf, pts:%lld", ts, frame->pts);
  
  // 渲染、UI相关的处理只能在主线程调度
  lms::async(lms::mainQueue(), this, [this, frame] () {
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

    SDL_Rect drawRect = calcDrawRect(contentMode, frame->width, frame->height, bounds);

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, &drawRect);
    SDL_RenderPresent(renderer);

    Uint32 t2 = SDL_GetTicks();

    av_frame_unref(frame);

    LMSLogDebug("Render cost: total=%2u, texture=%2u, present=%2u", t2 - t0, t1 - t0, t2 - t1);
  });
}

