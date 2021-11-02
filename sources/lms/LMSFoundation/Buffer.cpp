#include "LMSFoundation/Buffer.h"
#include "LMSFoundation/Logger.h"

namespace lms {

size_t FramesBuffer::numberOfCachedFrames() const {
  return cachedFrames.size();
}

void FramesBuffer::didReceiveFrame(Frame *frame) {
  LMSLogVerbose("Frame: %p", frame);
  
  auto avfrm  = av_frame_clone((AVFrame *)frame);
  cachedFrames.push_back(avfrm);
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
