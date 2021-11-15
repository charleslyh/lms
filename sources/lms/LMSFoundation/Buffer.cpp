#include "LMSFoundation/Buffer.h"
#include "LMSFoundation/Logger.h"

namespace lms {

FramesBuffer::FramesBuffer() {
  this->mtx = SDL_CreateMutex();
}

FramesBuffer::~FramesBuffer() {
  SDL_DestroyMutex(this->mtx);
}

size_t FramesBuffer::numberOfCachedFrames() const {
  size_t sz;
  SDL_LockMutex(mtx);
  {
    sz = cachedFrames.size();
  }
  SDL_UnlockMutex(mtx);
  return sz;
}

void FramesBuffer::pushBack(Frame *frame) {
  LMSLogVerbose("Frame: %p", frame);
  
  SDL_LockMutex(mtx);
  {
    auto avfrm  = av_frame_clone((AVFrame *)frame);
    cachedFrames.push_back(avfrm);
  }
  SDL_UnlockMutex(mtx);
}

Frame *FramesBuffer::popFrame() {
  Frame *frame = nullptr;
  
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

void FramesBuffer::refillFrame(Frame *frame) {
  SDL_LockMutex(mtx);
  {
    cachedFrames.push_back(frame);
  }
  SDL_UnlockMutex(mtx);
}

}
