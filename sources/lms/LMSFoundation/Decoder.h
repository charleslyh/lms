#pragma once

#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/MediaSource.h"
#include <list>

namespace lms {

struct DecoderMeta {
  std::string format;  // 例如 ffmpeg
  void       *data;
  int         width;
  int         height;
  int         pixelFormat;
  double      fps;
};

class DecoderDelegate : virtual public Object {
public:
  virtual void willStartDecodingPacket(Packet *packet) = 0;
  virtual void didFinishDecodingPacket(Packet *packet) = 0;
};

class Decoder : public PacketAcceptor, public FrameSource {
public:
  ~Decoder();

  void setDelegate(DecoderDelegate *delegate);

public:
  virtual DecoderMeta meta() = 0;
  virtual void start() = 0;
  virtual void stop() = 0;

protected:
  void notifyDecoderEvent(Packet *packet, int type);

private:
  DecoderDelegate *delegate = nullptr;
};

Decoder *createDecoder(const StreamMeta& meta);

}
