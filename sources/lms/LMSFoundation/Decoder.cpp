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
  void startDecoding() override;
  void stopDecoding() override;
  Metadata meta() override;
  
protected:
  void didReceivePacket(void *packet) override;
  
private:
  AVCodecParameters *params;
  AVCodecContext    *codecContext;
};

void FFMDecoder::startDecoding() {
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

void FFMDecoder::stopDecoding() {
  printf("MockDecoder::close()\n");
}

Metadata FFMDecoder::meta() {
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
 
  dispatchAsync(mainQueue(), [this, avpkt]() {
    notifyPacketDecodingEvent(avpkt, 0);
    
    int rt = avcodec_send_packet(codecContext, avpkt);
    if (rt < 0) {
      printf("Error sending packet for decoding: %d\n", rt);
      return;
    }
    while (rt >= 0) {
      AVFrame *frame = av_frame_alloc();
      rt = avcodec_receive_frame(codecContext, frame);
      if (rt == AVERROR(EAGAIN) || rt == AVERROR_EOF) {
        break;
      } else if (rt < 0) {
        printf("Error while decoding.\n");
        return;
      }

      printf("FFMDecoder::didReceivePacket | Did decode frame:%p\n", frame);
      deliverFrame(frame);
    }
    
    notifyPacketDecodingEvent(avpkt, 1);
  });
}

Decoder *createDecoder(const Metadata& meta) {
  // 根据meta信息匹配一个可创建，且最合适的解码器

  // 为了测试，返回一个假的解码器
  auto st = (AVStream *)meta.at("stream");
  return new FFMDecoder(st->codecpar);
}

}
