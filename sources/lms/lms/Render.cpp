#include "lms/Render.h"

namespace lms {

void Render::setDelegate(RenderDelegate *delegate) {
  this->delegate = delegate;
}

void Render::notifyRenderEvent(Frame *frame) {
  if (delegate != nullptr) {
    delegate->didRenderFrame(frame);
  }
}

}
