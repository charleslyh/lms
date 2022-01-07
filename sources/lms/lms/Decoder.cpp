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
  
  void decodeFrame() {
    AVFrame frame = {0};

    int rt = 0;
    do {
      rt = avcodec_receive_frame(codecContext, &frame);
      
      if (rt == 0) {
        break;
      } else if (rt == AVERROR(EAGAIN)) {
        AVPacket *avpkt = nullptr;

        int count = 0;
        SDL_LockMutex(packetsMutex);
        {
          if (!packets.empty()) {
            avpkt = packets.front();
            packets.pop_front();
            
            LMSLogVerbose("Packtes remain: type=%s, stream:%d, count=%u",
                          _media_type_name(stream->codecpar->codec_type), stream->index, (uint32_t)packets.size());
          }

          count = packets.size();
        }
        SDL_UnlockMutex(packetsMutex);
        
        if (avpkt) {
          fireEvent("did_update_packets", this, {
            { "stream_object", stream      },
            { "type"         , (uint64_t)2 }, /* decrement */
            { "count"        , (uint64_t)packets.size() },
            { "decrement"    , (uint64_t)1 },
          });
          
          cachingDuration -= avpkt->duration;
          double dt = cachingDuration * av_q2d(stream->time_base);
          LMSLogDebug("Duration changed: type=%s, dur=%lf", _media_type_name(stream->codecpar->codec_type), dt);        }
        
        if (avpkt == nullptr) {
          rt = AVERROR(EAGAIN);
          break;
        }

        rt = avcodec_send_packet(codecContext, avpkt);
        if (rt == AVERROR(EAGAIN)) {
          SDL_LockMutex(packetsMutex);
          {
            packets.push_front(avpkt);
            
            fireEvent("did_update_packets", this, {
              { "stream_object", stream      },
              { "type"         , (uint64_t)1 }, /* increment */
              { "count"        , (uint64_t)packets.size() },
              { "increment"    , (uint64_t)1 },
            });
          }
          
          cachingDuration += avpkt->duration;
          double dt = cachingDuration * av_q2d(stream->time_base);
          LMSLogDebug("Duration changed: type=%s, dur=%lf", _media_type_name(stream->codecpar->codec_type), dt);

          SDL_UnlockMutex(packetsMutex);
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
  
  int64_t               cachingDuration;
  std::list<AVPacket *> packets;
  SDL_mutex            *packetsMutex;
  void                 *obsSLNF;  // event observer: "should_load_next_frame"
};

void FFMDecoder::start() {
  LMSLogInfo("Start decoder | stream:%d, type:%d", stream->index, stream->codecpar->codec_type);
  
  packetsMutex = SDL_CreateMutex();

  int rt = avcodec_open2(codecContext, codec, 0);
  if (rt != 0) {
    LMSLogError("Couldn't open codec: %d", rt);
    return;
  }
  
  obsSLNF = addEventObserver("should_load_next_frame", nullptr, this, (EventCallback)onShouldLoadNextFrame);
  
  fireEvent("did_update_packets", this, {
    { "stream_object", stream      },
    { "type"         , (uint64_t)0 }, /* initial */
    { "count"        , (uint64_t)0 },
  });

  cachingDuration = 0;
}

void FFMDecoder::stop() {
  LMSLogInfo("Start decoder | stream:%d, type:%d", stream->index, stream->codecpar->codec_type);

  mainQueue()->cancel(this);

  // 需要在destroySemaphore前进行移除
  removeEventObserver(obsSLNF);

  SDL_DestroyMutex(packetsMutex);

  avcodec_close(codecContext);
}

void FFMDecoder::didReceivePipelineMessage(const PipelineMessage& msg) {
  auto srcpkt = (AVPacket *)msg.at("packet_object").value.ptr;
  assert(srcpkt != nullptr);
  
  size_t count = 0;
  SDL_LockMutex(packetsMutex);
  {
    // 由于下面的解码过程可能会被异步调度（延迟）处理，为了避免在函数‘立即’返回后，pkt资源被释放
    // 这里对pkt进行了一次人为clone
    AVPacket *avpkt = av_packet_clone(srcpkt);
    packets.push_back(avpkt);
    
    count = packets.size();
  }
  SDL_UnlockMutex(packetsMutex);

  fireEvent("did_update_packets", this, {
    { "stream_object", stream      },
    { "type"         , (uint64_t)1 }, /* increment */
    { "count"        , (uint64_t)count },
    { "increment"    , (uint64_t)1 },
  });
  
  cachingDuration += srcpkt->duration;
  double dt = cachingDuration * av_q2d(stream->time_base);
  LMSLogDebug("Duration changed: type=%s, dur=%lf", _media_type_name(stream->codecpar->codec_type), dt);
}

Cell *createDecoder(const StreamMeta& meta) {
  // 根据meta信息匹配一个可创建，且最合适的解码器

  // 为了测试，返回一个假的解码器
  auto st = (AVStream *)meta.at("stream_object").value.ptr;
  return new FFMDecoder(st);
}

}
