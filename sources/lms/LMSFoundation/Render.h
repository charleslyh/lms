#pragma once

#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Decoder.h"
#include "LMSFoundation/Logger.h"
#include <map>
#include <queue>
#include <string>

namespace lms {

class FramesBufferDelegate : virtual public Object {
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
  void didReceiveFrame(Frame *frame) override {
    LMSLogVerbose("frame: %p", frame);
    
    cachedFrames.push(frame);
    delegate->didTouchFrames(cachedFrames.size());
  }
  
private:
  FramesBufferDelegate *delegate = nullptr;
  std::queue<Frame *>   cachedFrames;
};

class RenderDelegate : virtual public Object {
public:
  virtual void didRenderFrame(Frame *frame) = 0;
};

class Render : virtual public Object {
public:
  void setDelegate(RenderDelegate *delegate);

  // TODO: VideoRender是否应该和Codec概念进行解耦？这意味着sartRendering接口不应该有codecMeta的概念
  // TODO: Render应该只做单纯的渲染工作，对buffer应该是无感知的
  virtual void startRendering(const Metadata& codecMeta, FramesBuffer *frameBuffer) = 0;
  virtual void stopRendering() = 0;

protected:
  void notifyRenderEvent(Frame *frame);

private:
  RenderDelegate *delegate = nullptr;
};

}
