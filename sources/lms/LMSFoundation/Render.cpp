#include "LMSFoundation/Render.h"

namespace lms {

void VideoRender::setDelegate(RenderDelegate *delegate) {
  this->delegate = delegate;
}

void VideoRender::notifyFrameRendered(void *frame) {
  if (delegate != nullptr) {
    delegate->didRenderFrame(frame);
  }
}

}
