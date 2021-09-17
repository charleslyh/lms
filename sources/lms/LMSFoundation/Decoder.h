#pragma once

#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/PacketSource.h"
#include <list>

namespace lms {

class FrameAcceptor : public Object {
public:
  virtual void didReceiveFrame(void *frame) = 0;
};

class DecoderDelegate {
public:
  virtual void willStartDecodingPacket(void *packet) = 0;
  virtual void didFinishDecodingPacket(void *packet) = 0;
};

class Decoder : public PacketAcceptor {
public:
  virtual void startDecoding() = 0;
  virtual void stopDecoding() = 0;
  virtual Metadata meta() = 0;

public:
  void setDelegate(DecoderDelegate *delegate) {
    this->delegate = delegate;
  }
  
  void addFrameAcceptor(FrameAcceptor *acceptor) {
    acceptors.push_back(acceptor);
  }

  void removeFrameAcceptor(FrameAcceptor *acceptor) {
    acceptors.remove(acceptor);
  }

protected:
  void deliverFrame(void *frame) {
    std::for_each(begin(acceptors), end(acceptors), [frame] (FrameAcceptor *acceptor) {
      acceptor->didReceiveFrame(frame);
    });
  }
  
  void notifyPacketDecodingEvent(void *packet, int type) {
    if (delegate == nullptr) {
      return;
    }
    
    if (type == 0) {
      delegate->willStartDecodingPacket(packet);
    } else if (type == 1) {
      delegate->didFinishDecodingPacket(packet);
    }
  }

private:
  DecoderDelegate *delegate = nullptr;
  std::list<FrameAcceptor *> acceptors;
};

Decoder *createDecoder(const Metadata& meta);

}
