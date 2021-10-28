#pragma once

#include "LMSFoundation/Foundation.h"

namespace lms {

class StreamMeta;

class RenderDelegate : virtual public Object {
public:
  virtual void didRenderFrame(Frame *frame) = 0;
};

class Render : public FrameAcceptor {
public:
  void setDelegate(RenderDelegate *delegate);

  // TODO: VideoRender是否应该和Codec概念进行解耦？这意味着sartRendering接口不应该有codecMeta的概念
  // TODO: Render应该只做单纯的渲染工作，对buffer应该是无感知的
  virtual void start(const StreamMeta& meta) = 0;
  virtual void stop() = 0;

protected:
  void notifyRenderEvent(Frame *frame);

private:
  RenderDelegate *delegate = nullptr;
};

}
