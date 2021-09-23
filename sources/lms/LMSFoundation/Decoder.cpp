#include "LMSFoundation/Decoder.h"
#include "LMSFoundation/Logger.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <map>

namespace lms {

class FFMDecoder : public Decoder {
public:
  FFMDecoder(AVCodecParameters *codecParams) {
    this->params       = codecParams;
    
    codec = avcodec_find_decoder(params->codec_id);
    if (codec == nullptr) {
      LMSLogError("Unsupported codec: %d", params->codec_id);
      return;
    }

    codecContext = avcodec_alloc_context3(codec);
    int rt = avcodec_parameters_to_context(codecContext, params);
    if (rt != 0) {
      LMSLogError("Couldn't copy codec context: %d", rt);
      return;
    }
  }
  
protected:
  void start() override;
  void stop() override;
  DecoderMeta meta() override;
  
protected:
  void didReceivePacket(Packet *packet) override;
  
private:
  AVCodecParameters *params;
  AVCodecContext    *codecContext;
  AVCodec *codec;
};

void FFMDecoder::start() {
  LMSLogInfo("startDecoding");

  int rt = avcodec_open2(codecContext, codec, nullptr);
  if (rt != 0) {
    LMSLogError("Couldn't open codec: %d", rt);
    return;
  }
}

void FFMDecoder::stop() {
  LMSLogInfo("stopDecoding");
  avcodec_close(codecContext);
}

DecoderMeta FFMDecoder::meta() {
  return {
    "ffmpeg",
    (void *)codecContext,
    params->width,
    params->height,
  };
}

void FFMDecoder::didReceivePacket(Packet *packet) {
  LMSLogVerbose("packet: %p", packet);

  if (packet == nullptr) {
    return;
  }
  
  auto avpkt = (AVPacket *)packet;
 
  dispatchAsync(mainQueue(), [this, avpkt]() {
    notifyDecoderEvent(avpkt, 0);
    LMSLogVerbose("Begin decoding frame: %p", avpkt);

    int rt = avcodec_send_packet(codecContext, avpkt);
    if (rt < 0) {
      LMSLogError("Error sending packet for decoding: %d", rt);
      return -1;
    }
    while (rt >= 0) {
      AVFrame *frame = av_frame_alloc();
      rt = avcodec_receive_frame(codecContext, frame);
      if (rt == AVERROR(EAGAIN) || rt == AVERROR_EOF) {
        break;
      } else if (rt < 0) {
        LMSLogError("Error while decoding: %d", rt);
        return -1;
      }

      deliverFrame(frame);
    }
    
    LMSLogVerbose("End decoding frame: %p", avpkt);
    notifyDecoderEvent(avpkt, 1);
    return 0;
  });
}

Decoder::~Decoder() {
  setDelegate(nullptr);
}

void Decoder::setDelegate(DecoderDelegate *delegate) {
  if (this->delegate != nullptr) {
    lms::release(this->delegate);
  }
  
  this->delegate = lms::retain(delegate);
}

void Decoder::notifyDecoderEvent(Packet *packet, int type) {
  if (delegate == nullptr) {
    return;
  }
  
  if (type == 0) {
    delegate->willStartDecodingPacket(packet);
  } else if (type == 1) {
    delegate->didFinishDecodingPacket(packet);
  }
}

Decoder *createDecoder(const StreamMeta& meta) {
  // 根据meta信息匹配一个可创建，且最合适的解码器

  // 为了测试，返回一个假的解码器
  auto st = (AVStream *)meta.data;
  return new FFMDecoder(st->codecpar);
}

}
