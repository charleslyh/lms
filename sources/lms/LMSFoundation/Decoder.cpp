#include "LMSFoundation/Decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace lms {

class FFMDecoder : public Decoder {
public:
  FFMDecoder(AVCodecParameters *codecParams) {
    this->params = codecParams;
  }
  
protected:
  void open() override;
  void close() override;
  std::map<std::string, void*> meta() override;
  
protected:
  void didReceivePacket(void *packet) override;
  
private:
  AVCodecParameters *params;
  AVCodecContext    *codecContext;
};

void FFMDecoder::open() {
  printf("MockDecoder::open()\n");
  
  AVCodec *codec = nullptr;
  codec = avcodec_find_decoder(params->codec_id);
  if (codec == nullptr) {
    printf("Unsupported codec.\n");
    return;
  }

  // retrieve codec context
  codecContext = avcodec_alloc_context3(codec);
  int rt = avcodec_parameters_to_context(codecContext, params);
  if (rt != 0) {
    printf("Could not copy codec context: %d\n", rt);
    return;
  }

  rt = avcodec_open2(codecContext, codec, nullptr);
  if (rt != 0) {
      printf("Could not open codec: %d\n", rt);
      return;
  }
}

void FFMDecoder::close() {
  printf("MockDecoder::close()\n");
}

std::map<std::string, void*> FFMDecoder::meta() {
  return {
    {"codec_context", (void*) codecContext},
  };
}

void FFMDecoder::didReceivePacket(void *packet) {
  printf("MockDecoder::onReceivePacket(packet: %p)\n", packet);
  
  if (packet == nullptr) {
    return;
  }
  
  auto avpkt = (AVPacket *)packet;

  printf("Packet {stream:%d, size:%d, pts:%lld, dts:%lld}\n"
     , avpkt->stream_index
     , avpkt->size
     , avpkt->pts
     , avpkt->dts);
  
  int rt = avcodec_send_packet(codecContext, avpkt);
  if (rt < 0) {
    printf("Error sending packet for decoding: %d\n", rt);
    return;
  }
  AVFrame *frame = av_frame_alloc();
  while (rt >= 0) {
    rt = avcodec_receive_frame(codecContext, frame);
    if (rt == AVERROR(EAGAIN) || rt == AVERROR_EOF) {
      return;
    } else if (rt < 0) {
      printf("Error while decoding.\n");
      return;
    }

    deliverFrame(frame);
  }
}

Decoder *createDecoder(std::map<std::string, void*> meta) {
  // 根据meta信息匹配一个可创建，且最合适的解码器

  // 为了测试，返回一个假的解码器
  auto st = (AVStream *)meta["stream"];
  return new FFMDecoder(st->codecpar);
}

}
