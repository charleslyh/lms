#pragma once

#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/PacketSource.h"
#include <list>

namespace lms {

class DecoderDelegate : virtual public Object {
public:
  virtual void willStartDecodingPacket(Packet *packet) = 0;
  virtual void didFinishDecodingPacket(Packet *packet) = 0;
};

class Decoder : public PacketAcceptor, public FrameOutput {
public:
  ~Decoder();

  void setDelegate(DecoderDelegate *delegate);

public:
  virtual Metadata meta() = 0;
  virtual void start() = 0;
  virtual void stop() = 0;

protected:
  void notifyDecoderEvent(Packet *packet, int type);

private:
  DecoderDelegate *delegate = nullptr;
};

Decoder *createDecoder(const Metadata& meta);

}
