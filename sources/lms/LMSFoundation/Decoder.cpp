#include "LMSFoundation/Decoder.h"

extern "C" {
#include <libavformat/avformat.h>
}

namespace lms {

class FFMDecoder : public Decoder {
public:
  void open() override;
  void close() override;
  void onReceivePacket(PacketSource *src, void *packet) override;
};

void FFMDecoder::open() {
  printf("MockDecoder::open()\n");
}

void FFMDecoder::close() {
  printf("MockDecoder::close()\n");
}

void FFMDecoder::onReceivePacket(PacketSource *src, void *packet) {
  printf("MockDecoder::onReceivePacket(packet: %p)\n", packet);
  
  AVPacket& sharedPacket = *((AVPacket *)packet);

  printf("Packet {stream:%d, size:%d, pts:%lld, dts:%lld}\n"
     , sharedPacket.stream_index
     , sharedPacket.size
     , sharedPacket.pts
     , sharedPacket.dts);

  ((PassivePacketSource *)src)->loadPackets(1);
}

Decoder *createDecoder(PacketSource *source) {
  auto meta = source->metadata();

  // 根据meta信息匹配一个可创建，且最合适的解码器

  // 为了测试，返回一个假的解码器
  return new FFMDecoder();
}

}
