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
  
  auto avfrm  = av_frame_clone((AVFrame *)frame);
  cachedFrames.push_back(avfrm);
}

bool FramesBuffer::squeezeFrame(uint64_t pts) {
  Frame *frame = nullptr;
  if (cachedFrames.empty()) {
    LMSLogDebug("No frames available!");
    return false;
  }
  
  frame = cachedFrames.front();
  cachedFrames.pop_front();

  LMSLogDebug("Frame popped, remains: %lu", cachedFrames.size());
  deliverFrame(frame);
  
  return true;
}

Frame *FramesBuffer::popFrame() {
  if (cachedFrames.empty()) {
    LMSLogDebug("No frames available!");
    return nullptr;
  } else {
    Frame *frame = cachedFrames.front();
    cachedFrames.pop_front();
    LMSLogDebug("Frame popped, remains: %lu", cachedFrames.size());
    return frame;
  }
}

void FramesBuffer::refillFrame(Frame *frame) {
  cachedFrames.push_back(frame);
}

}
