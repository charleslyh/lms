#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Player.h"
#include "SDLApplication.h"

extern "C" {
  #include <libavutil/imgutils.h>
  #include <libswscale/swscale.h>
  #include <SDL2/SDL.h>
}

class SDLView : public lms::Render {
protected:
  void prepare(std::map<std::string, void *> codecMeta) override {
    cc = (AVCodecContext *)codecMeta["codec_context"];
    
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
  
  void teardown() override {
  }

protected:
  void didReceiveFrame(void *f) override {
    auto frame = (AVFrame *)f;
    printf("SDLView::didReceiveFrame(%p)\n", frame);
    
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

class PlayerAppDelegate: public SDLAppDelegate, public lms::DispatchQueue {
public:
  void didFinishLaunchingApplication(int argc, char **argv) override {
    lms::init({this});

    lms::VideoFile *src = lms::autoRelease(new lms::VideoFile(argv[1]));
    player = new lms::Player(src);
    player->setVideoRender(lms::autoRelease(new SDLView));
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

private:
  lms::Player *player;
};


int main(int argc, char *argv[]) {
  SDLApplication app(argc, argv);
  PlayerAppDelegate delegate;
  app.run(&delegate);
}

