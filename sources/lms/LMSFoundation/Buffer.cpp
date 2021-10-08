#include "LMSFoundation/Buffer.h"
#include "LMSFoundation/Logger.h"

namespace lms {

void FramesBuffer::setIdealBufferingFrames(int idealBufferingFrames) {
  this->idealBufferingFrames = idealBufferingFrames;
}

size_t FramesBuffer::numberOfCachedFrames() const {
  return cachedFrames.size();
}

size_t FramesBuffer::numberOfEmptySlots() const {
  return idealBufferingFrames - numberOfCachedFrames();
}

void FramesBuffer::didReceiveFrame(Frame *frame) {
  LMSLogVerbose("Frame: %p", frame);
  
  cachedFrames.push_back(frame);
}

bool FramesBuffer::squeezeFrame(uint64_t pts) {
  Frame *frame = nullptr;
  if (cachedFrames.empty()) {
    LMSLogDebug("No frames available!");
    return false;
  }
  
  frame = cachedFrames.front();
  cachedFrames.pop_front();

  LMSLogDebug("Frame: %p", frame);
  deliverFrame(frame);
  
  return true;
}

}
