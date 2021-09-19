#pragma once

#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/PacketSource.h"
#include <list>

namespace lms {

class FrameAcceptor : virtual public Object {
public:
  virtual void didReceiveFrame(Frame *frame) = 0;
};

class DecoderDelegate : virtual public Object {
public:
  virtual void willStartDecodingPacket(Packet *packet) = 0;
  virtual void didFinishDecodingPacket(Packet *packet) = 0;
};

class Decoder : public PacketAcceptor {
public:
  ~Decoder();

  void setDelegate(DecoderDelegate *delegate);
  void addFrameAcceptor(FrameAcceptor *acceptor);
  void removeFrameAcceptor(FrameAcceptor *acceptor);

public:
  virtual Metadata meta() = 0;
  virtual void startDecoding() = 0;
  virtual void stopDecoding() = 0;

protected:
  void deliverFrame(Frame *frame);
  void notifyDecoderEvent(Packet *packet, int type);

private:
  DecoderDelegate *delegate = nullptr;
  std::list<FrameAcceptor *> acceptors;
};

Decoder *createDecoder(const Metadata& meta);

}
