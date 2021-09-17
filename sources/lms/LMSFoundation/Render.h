#pragma once

#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Decoder.h"
#include <map>
#include <queue>
#include <string>

namespace lms {

class FramesBufferDelegate : public Object {
public:
  virtual void didTouchFrames(size_t newSize) = 0;
};

class FramesBuffer : public FrameAcceptor {
public:
  Frame *popFrame(uint64_t ts, uint32_t tolorence) {
    Frame *frame = nullptr;
    if (!cachedFrames.empty()) {
       frame = cachedFrames.front();
      cachedFrames.pop();
      
      printf("FramesBuffer::popFrame -> frame: %p, newSize:%lu\n", frame, cachedFrames.size());
    }

    delegate->didTouchFrames(cachedFrames.size());

    return frame;
  }
  
  size_t numberOfFrames() const {
    return cachedFrames.size();
  }
  
  void setDelegate(FramesBufferDelegate *delegate) {
    this->delegate = lms::retain(delegate);
  }
  
protected:
  void didReceiveFrame(void *frame) override {
    cachedFrames.push(frame);
    printf("FramesBuffer::didReceiveFrame(%p) | newSize: %lu\n", frame, cachedFrames.size());

    delegate->didTouchFrames(cachedFrames.size());
  }
  
private:
  FramesBufferDelegate *delegate = nullptr;
  std::queue<Frame *>   cachedFrames;
};

class RenderDelegate : public Object {
public:
  virtual void didRenderFrame(void *frame) = 0;
};

class VideoRender : public Object {
public:
  void setDelegate(RenderDelegate *delegate);

public:
  virtual void startRendering(const Metadata& codecMeta, FramesBuffer *frameBuffer) = 0;
  virtual void stopRendering() = 0;
  
protected:
  void notifyFrameRendered(void *frame);

private:
  RenderDelegate *delegate = nullptr;
};

}
