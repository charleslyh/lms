#pragma once
#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Decoder.h"
#include "LMSFoundation/Logger.h"
extern "C" {
#include <SDL2/SDL.h>
}

namespace lms {

template<class T>
class FramesBuffer : virtual public Object {
public:
  FramesBuffer()  {
    this->mtx = SDL_CreateMutex();
  }
  
  ~FramesBuffer() {
    SDL_DestroyMutex(this->mtx);
  }
  
  size_t numberOfCachedFrames() const {
    size_t sz;
    SDL_LockMutex(mtx);
    {
      sz = cachedFrames.size();
    }
    SDL_UnlockMutex(mtx);
    return sz;
  }
   
  Frame* popFrame() {
    T frame = nullptr;
    
    SDL_LockMutex(mtx);
    {
      if (cachedFrames.empty()) {
        LMSLogDebug("No frames available!");
      } else {
        frame = cachedFrames.front();
        cachedFrames.pop_front();
        LMSLogDebug("Frame popped, remains: %lu", cachedFrames.size());
      }
    }
    SDL_UnlockMutex(mtx);

    return frame;
  }
  
  void refillFrame(T frame) {
    SDL_LockMutex(mtx);
    {
      cachedFrames.push_back(frame);
    }
    SDL_UnlockMutex(mtx);
  }
  
  void pushBack(T frame) {
    LMSLogVerbose("Frame: %p", frame);
    
    SDL_LockMutex(mtx);
    {
      auto avfrm  = av_frame_clone(frame);
      cachedFrames.push_back(avfrm);
    }
    SDL_UnlockMutex(mtx);
  }

private:
  int idealBufferingFrames;
  std::list<T> cachedFrames;
  SDL_mutex *mtx;
};

}
