#include "Decoder.h"
#include "Logger.h"
#include "Runtime.h"
#include "Events.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <SDL2/SDL.h>
}
#include <inttypes.h>

namespace lms {

static const char *_media_type_name(int media_type) {
  const char *mediaType = "Unkonwn";
  if (media_type == AVMEDIA_TYPE_VIDEO) {
    mediaType = "Video";
  } else if (media_type == AVMEDIA_TYPE_AUDIO) {
    mediaType = "Audio";
  }
  return mediaType;
}

class FFMDecoder : public Cell {
public:
  FFMDecoder(AVStream *stream) {
    this->stream = stream;
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
  }
  
protected:
  void start() override;
  void stop() override;
  
  void didReceivePipelineMessage(const PipelineMessage& msg) override;
  
private:
  static void onShouldLoadNextFrame(FFMDecoder *self, const char *evtName, void *sender, const EventParams& p) {
    AVStream *streamObject = (AVStream *)variantsGetPointer(p, "stream_object");
    
    // 仅当发起方为对应流时，才应该激活解码处理，否则可能会提前解码数据帧
    if (streamObject == self->stream) {
      self->decodeFrame();
    }
  }
  
  void notifyPacketsUpdated(uint64_t type) {
    EventParams p = {
      { "stream_object", stream },
      { "type"         , type   },
      { "count"        , (uint64_t)packets.size() },
    };
    
    if (type == 1) {
      p["increment"] = 1;
    } else if (type == 2) {
      p["decrement"] = decrements;
      decrements = 0;
    }
    
    fireEvent("did_update_packets", this, p);
  }
  
  void pushPacket(AVPacket *packet) {
    packets.push_back(packet);
       
    LMSLogVerbose("Push packet: type=%s, stream:%d, count=%u",
                  _media_type_name(stream->codecpar->codec_type), stream->index, (uint32_t)packets.size());
  }
  
  AVPacket *popPacket() {
    AVPacket *packet = nullptr;

    if (!packets.empty()) {
      packet = packets.front();
      packets.pop_front();
    }
    
    decrements += 1;
    if (decrements >=10) {
      notifyPacketsUpdated(2);
    }
    
    LMSLogVerbose("Pop packet: type=%s, stream:%d, count=%u",
                  _media_type_name(stream->codecpar->codec_type), stream->index, (uint32_t)packets.size());

    return packet;
  }
  
  void refillPacket(AVPacket *packet) {
    packets.push_front(packet);

    LMSLogVerbose("Refill packet: type=%s, stream:%d, count=%u",
                  _media_type_name(stream->codecpar->codec_type), stream->index, (uint32_t)packets.size());
  }
  
  void decodeFrame() {
    assert(isMainThread());

    AVFrame frame = {0};

    int rt = 0;
    do {
      rt = avcodec_receive_frame(codecContext, &frame);
      
      if (rt == 0) {
        break;
      } else if (rt == AVERROR(EAGAIN)) {
        AVPacket *avpkt = popPacket();

        if (avpkt == nullptr) {
          rt = AVERROR(EAGAIN);
          break;
        }

        rt = avcodec_send_packet(codecContext, avpkt);
        if (rt == AVERROR(EAGAIN)) {
          refillPacket(avpkt);
        } else {
          // 该数据包已被正常消耗，应进行释放
          av_packet_free(&avpkt);

          if (rt != 0) {
            break;
          }
        }
      } else {
        LMSLogError("Error while decoding: stream:%d, code=%d", stream->index, rt);
        break;
      }
    } while (true);
    
    if (rt == 0) {
      uint32_t now = SDL_GetTicks();
      LMSLogDebug("Frame decoded: type=%s, stream:%d, pts=%" PRIi64,
                  _media_type_name(stream->codecpar->codec_type), stream->index, frame.pts);
      
      PipelineMessage frameMsg;
      frameMsg["type"]  = "media_frame";
      frameMsg["frame"] = &frame;
      deliverPipelineMessage(frameMsg);
      av_frame_unref(&frame);
    }
    
    return rt == 0 || rt == AVERROR(EAGAIN);
  }
  
private:
  AVStream *stream;
  AVCodecParameters *params;
  AVCodecContext *codecContext;
  AVCodec *codec;
  
  int                   decrements;
  int64_t               cachingDuration;
  std::list<AVPacket *> packets;
  void                 *obsSLNF;  // event observer: "should_load_next_frame"
};

void FFMDecoder::start() {
  LMSLogInfo("Start decoder | stream:%d, type:%d", stream->index, stream->codecpar->codec_type);
  
  int rt = avcodec_open2(codecContext, codec, 0);
  if (rt != 0) {
    LMSLogError("Couldn't open codec: %d", rt);
    return;
  }
  
  obsSLNF = addEventObserver("should_load_next_frame", nullptr, this, (EventCallback)onShouldLoadNextFrame);
  
  decrements = 0;
  notifyPacketsUpdated(0);
}

void FFMDecoder::stop() {
  LMSLogInfo("Start decoder | stream:%d, type:%d", stream->index, stream->codecpar->codec_type);

  mainQueue()->cancel(this);

  // 需要在destroySemaphore前进行移除
  removeEventObserver(obsSLNF);

  avcodec_close(codecContext);
}

void FFMDecoder::didReceivePipelineMessage(const PipelineMessage& msg) {
  assert(isMainThread());
  
  auto srcpkt = (AVPacket *)msg.at("packet_object").value.ptr;
  AVPacket *avpkt = av_packet_clone(srcpkt);
  assert(avpkt != nullptr);
  pushPacket(avpkt);
}

Cell *createDecoder(const StreamMeta& meta) {
  // 根据meta信息匹配一个可创建，且最合适的解码器

  // 为了测试，返回一个假的解码器
  auto st = (AVStream *)meta.at("stream_object").value.ptr;
  return new FFMDecoder(st);
}

}
