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
  FramesBuffer(const char *client = "unknown")  {
    this->mtx = SDL_CreateMutex();
    this->client = strdup(client);
  }
  
  ~FramesBuffer() {
    free(this->client);
    SDL_DestroyMutex(this->mtx);
  }
  
  size_t count() const {
    size_t sz;
    SDL_LockMutex(mtx);
    {
      sz = cachedFrames.size();
    }
    SDL_UnlockMutex(mtx);
    return sz;
  }
   
  T popFront() {
    T frame = nullptr;
    
    SDL_LockMutex(mtx);
    {
      if (cachedFrames.empty()) {
        LMSLogDebug("No frames available!");
      } else {
        frame = cachedFrames.front();
        cachedFrames.pop_front();
        LMSLogDebug("Frame popped, client: %s, remains: %lu", client, cachedFrames.size());
      }
    }
    SDL_UnlockMutex(mtx);

    return frame;
  }
  
  void pushFront(T frame) {
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
      cachedFrames.push_back(frame);
    }
    SDL_UnlockMutex(mtx);
  }

private:
  std::list<T> cachedFrames;
  SDL_mutex *mtx;
  char *client;
};

}
