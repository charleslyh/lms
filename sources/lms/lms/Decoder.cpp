#include "Decoder.h"
#include "Logger.h"
#include "Runtime.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <inttypes.h>

namespace lms {

class FFMDecoder : public Cell {
public:
  FFMDecoder(AVStream *stream) {
    this->params = stream->codecpar;
    
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
    
    frameDecoded = av_frame_alloc();
  }
  
protected:
  void start() override;
  void stop() override;
  
  void didReceivePipelineMessage(const PipelineMessage& msg) override;
  
private:
  AVCodecParameters *params;
  AVCodecContext *codecContext;
  AVCodec *codec;
  AVFrame *frameDecoded;
};

void FFMDecoder::start() {
  LMSLogInfo("decoder=%p", this);

  int rt = avcodec_open2(codecContext, codec, 0);
  if (rt != 0) {
    LMSLogError("Couldn't open codec: %d", rt);
    return;
  }
}

void FFMDecoder::stop() {
  LMSLogInfo("decoder=%p", this);

  // TODO: 完整的流程应该是
  // TODO: 1. 置状态为stopping
  // TODO: 2. didReceivePacket拒处理新的数据包（也许应该由上层来保证？例如先拆除掉上游节点）
  // TODO: 3. drain queue中所有的解码 blocks，且解码block应该正确判断状态，仅当running的时候才可以解码
  // TODO: 4. 置状态未stopped
  // TODO: 5. 释放其它资源
  lms::mainQueue()->cancel(this);

  avcodec_close(codecContext);
}

void FFMDecoder::didReceivePipelineMessage(const PipelineMessage& msg) {
  auto srcpkt = (AVPacket *)msg.at("packet_object").value.ptr;
  assert(srcpkt != nullptr);

  // 由于下面的解码过程可能会被异步调度（延迟）处理，为了避免在函数‘立即’返回后，pkt资源被释放
  // 这里对pkt进行了一次人为引用（或者需要clone？）
  AVPacket *avpkt = av_packet_clone(srcpkt);
  std::shared_ptr<AVPacket> pktHolder(avpkt, [] (AVPacket *pkt) { av_packet_free(&pkt); });
  
  // TODO: 使用独立的queue来进行解码
  async(mainQueue(), this, [this, avpkt, pktHolder]() {
    LMSLogDebug("Begin decode: stream=%d, flags=0x%02X, duration=%" PRIi64 ", pts=%" PRIi64,
                avpkt->stream_index, avpkt->flags, avpkt->duration, avpkt->pts);

    int rt = avcodec_send_packet(codecContext, avpkt);
    if (rt < 0) {
      LMSLogError("Error sending packet for decoding: %d", rt);
    }

    AVFrame *frame = frameDecoded;
    while (rt >= 0) {
      rt = avcodec_receive_frame(codecContext, frame);
      if (rt == AVERROR(EAGAIN)) {
        // 数据不足，需要继续喂数据才能完成解码
        break;
      } else if (rt == AVERROR_EOF) {
        // 已读取完所有数据
        break;
      } else if (rt < 0) {
        LMSLogError("Error while decoding: %d", rt);
        break;
      }
            
      PipelineMessage frameMsg;
      frameMsg["type"]  = "media_frame";
      frameMsg["frame"] = frame;
      deliverPipelineMessage(frameMsg);
      av_frame_unref(frame);
    }
    
    LMSLogDebug("End decode: stream=%d, pts=%" PRIi64, avpkt->stream_index, avpkt->pts);
  });
}

Cell *createDecoder(const StreamMeta& meta) {
  // 根据meta信息匹配一个可创建，且最合适的解码器

  // 为了测试，返回一个假的解码器
  auto st = (AVStream *)meta.at("stream_object").value.ptr;
  return new FFMDecoder(st);
}

}
