#pragma once
#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Decoder.h"

namespace lms {
 
class FramesBuffer : public FrameAcceptor, public FrameSource {
public:
  size_t numberOfCachedFrames() const;
  size_t numberOfEmptySlots() const;
  
  bool squeezeFrame(uint64_t pts);
  
  Frame* popFrame();
  void refillFrame(Frame *frame);

protected:
  void setIdealBufferingFrames(int maxBufferingFrames);
  
protected:
  void didReceiveFrame(Frame *frame) override;

private:
  int idealBufferingFrames;
  std::list<Frame *> cachedFrames;
};

}
