//
//  VideoRenderDriver.hpp
//  lms
//
//  Created by yuhuachli on 2022/1/14.
//

#pragma once
#include "Cell.h"

FWD_DECLARE_STRUCT(AVStream);
FWD_DECLARE_STRUCT(AVFrame);
FWD_DECLARE_STRUCT(SDL_mutex);

namespace lms {

class TimeSync;
class Timer;
class DispatchQueue;

class VideoRenderDriver : public Cell {
public:
  VideoRenderDriver(AVStream *stream, Cell *videoRender, TimeSync *timeSync);
  ~VideoRenderDriver();
  
public:
  void start() override;
  void stop() override;
  void didReceivePipelineMessage(const PipelineMessage& msg) override;
 
private:
  AVStream *stream;
  Cell     *render;
  Timer    *fpsTimer;
  TimeSync *timeSync;
  
  SDL_mutex *frameMutex;
  AVFrame   *nextFrame;
  
  DispatchQueue *q;
};

}
