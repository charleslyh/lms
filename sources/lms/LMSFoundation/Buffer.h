#pragma once
#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Decoder.h"
extern "C" {
#include <SDL2/SDL.h>
}

namespace lms {
 
class FramesBuffer : virtual public Object {
public:
  FramesBuffer();
  ~FramesBuffer();
  
  size_t numberOfCachedFrames() const;
   
  Frame* popFrame();
  void refillFrame(Frame *frame);
  
  void pushBack(Frame *frame);

private:
  int idealBufferingFrames;
  std::list<Frame *> cachedFrames;
  SDL_mutex *mtx;
};

}
