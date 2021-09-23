#pragma once
#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Decoder.h"

namespace lms {
 
class FramesBuffer : public FrameAcceptor, public FrameSource {
public:
  size_t numberOfFrames() const;
  void squeezeFrame(uint64_t pts);
  
protected:
  void didReceiveFrame(Frame *frame) override;

private:
  std::list<Frame *> cachedFrames;
};

}
