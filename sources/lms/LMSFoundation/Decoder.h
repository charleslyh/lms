#pragma once

#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/PacketSource.h"
#include <list>

namespace lms {

class FrameAcceptor : public Object {
public:
  virtual void didReceiveFrame(void *frame) = 0;
};

class Decoder : public PacketAcceptor {
public:
  virtual void open() = 0;
  virtual void close() = 0;

public:
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

private:
  std::list<FrameAcceptor *> acceptors;
};

Decoder *createDecoder(PacketSource *source);

}
