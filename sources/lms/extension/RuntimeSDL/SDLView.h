#pragma once

#include <lms/Cell.h>
extern "C" {
#include <SDL2/SDL.h>
#include <libavformat/avformat.h>
}

class SWSFrameScaler;

class SDLView : public lms::Cell {
public:
  typedef enum {
    aspectFit   = 1,
    aspectFill  = 2,
    scaleToFill = 3,
  } ContentMode;

  ContentMode getContentMode() const;
  void setContentMode(ContentMode contentMode);

protected:
  void configure(const lms::StreamMeta& meta) override;
  void start() override;
  void stop() override;
  
protected:
  void didReceivePipelineMessage(const lms::PipelineMessage& cmsg) override;
  
private:
  AVStream *st;
  SDL_Window *win;
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *texture = nullptr;
  SWSFrameScaler *scaler = nullptr;
  ContentMode contentMode = aspectFit;
};
