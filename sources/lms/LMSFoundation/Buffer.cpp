#include "LMSFoundation/Buffer.h"
#include "LMSFoundation/Logger.h"

namespace lms {

size_t FramesBuffer::numberOfFrames() const {
  return cachedFrames.size();
}

void FramesBuffer::didReceiveFrame(Frame *frame) {
  LMSLogVerbose("Frame: %p", frame);
  
  cachedFrames.push_back(frame);
}

void FramesBuffer::squeezeFrame(uint64_t pts) {
  Frame *frame = nullptr;
  if (cachedFrames.empty()) {
    LMSLogDebug("No frames available!");
    return;
  }
  
  frame = cachedFrames.front();
  cachedFrames.pop_front();

  LMSLogDebug("Frame: %p", frame);
  deliverFrame(frame);
}

}
