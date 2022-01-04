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
  bool tryDecodeFrame() {
    AVFrame frame = {0};

    int rt = 0;
    do {
      rt = avcodec_receive_frame(codecContext, &frame);
      
      if (rt == 0) {
        break;
      } else if (rt == AVERROR(EAGAIN)) {
        AVPacket *avpkt = nullptr;

        SDL_LockMutex(packetsMutex);
        {
          if (!packets.empty()) {
            avpkt = packets.front();
            packets.pop_front();
            
            LMSLogVerbose("Packtes remain: stream:%d, count=%u", stream->index, (uint32_t)packets.size());
          }
        }
        SDL_UnlockMutex(packetsMutex);
        
        if (avpkt == nullptr) {
          rt = AVERROR(EAGAIN);
          break;
        }

        rt = avcodec_send_packet(codecContext, avpkt);
        if (rt == AVERROR(EAGAIN)) {
          SDL_LockMutex(packetsMutex);
          {
            packets.push_front(avpkt);
          }
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
      LMSLogDebug("Frame decoded: stream:%d, pts=%" PRIi64, stream->index, frame.pts);
      
      PipelineMessage frameMsg;
      frameMsg["type"]  = "media_frame";
      frameMsg["frame"] = &frame;
      deliverPipelineMessage(frameMsg);
      av_frame_unref(&frame);
    }
    
    return rt == 0 || rt == AVERROR(EAGAIN);
  }
  
  static int decodingThreadProc(FFMDecoder *self) {
    while(true) {
      SDL_SemWait(self->sem);
      if (!self->decoding) {
        break;
      }
      
      if (!self->tryDecodeFrame()) {
        break;
      }
    }
    
    return 0;
  }

private:
  AVStream *stream;
  AVCodecParameters *params;
  AVCodecContext *codecContext;
  AVCodec *codec;
  
  std::list<AVPacket *> packets;
  SDL_mutex            *packetsMutex;
  SDL_sem              *sem;
  SDL_Thread           *thread;
  bool                 decoding;
  void                 *obsSLNF;  // event observer: "should_load_next_frame"
};

void FFMDecoder::start() {
  LMSLogInfo("Start decoder | stream:%d, type:%d", stream->index, stream->codecpar->codec_type);
  
  thread       = nullptr;
  packetsMutex = SDL_CreateMutex();
  sem          = SDL_CreateSemaphore(0);

  int rt = avcodec_open2(codecContext, codec, 0);
  if (rt != 0) {
    LMSLogError("Couldn't open codec: %d", rt);
    return;
  }
  
  obsSLNF = addEventObserver("should_load_next_frame", nullptr, [this] (const char *name, void *sender, const EventParams& p) {
    AVStream *streamObject = (AVStream *)variantsGetPointer(p, "stream_object");
    
    // 仅当发起方为对应流时，才应该激活解码处理，否则可能会提前解码数据帧
    if (streamObject == this->stream) {
      SDL_SemPost(this->sem);
    }
  });
  
  const char *tname = params->codec_type == AVMEDIA_TYPE_VIDEO ? "DecodeVideo" : "DecodeAudio";
  decoding = true;
  thread   = SDL_CreateThread((SDL_ThreadFunction)FFMDecoder::decodingThreadProc, tname, this);
}

void FFMDecoder::stop() {
  LMSLogInfo("Start decoder | stream:%d, type:%d", stream->index, stream->codecpar->codec_type);

  // 终止解码线程
  decoding = false;
  SDL_SemPost(sem);
  SDL_WaitThread(thread, nullptr);

  // 需要在destroySemaphore前进行移除
  removeEventObserver(obsSLNF);

  SDL_DestroyMutex(packetsMutex);
  SDL_DestroySemaphore(sem);

  avcodec_close(codecContext);
}

void FFMDecoder::didReceivePipelineMessage(const PipelineMessage& msg) {
  auto srcpkt = (AVPacket *)msg.at("packet_object").value.ptr;
  assert(srcpkt != nullptr);

  SDL_LockMutex(packetsMutex);
  {
    // 由于下面的解码过程可能会被异步调度（延迟）处理，为了避免在函数‘立即’返回后，pkt资源被释放
    // 这里对pkt进行了一次人为clone
    AVPacket *avpkt = av_packet_clone(srcpkt);
    packets.push_back(avpkt);
  }
  SDL_UnlockMutex(packetsMutex);
}

Cell *createDecoder(const StreamMeta& meta) {
  // 根据meta信息匹配一个可创建，且最合适的解码器

  // 为了测试，返回一个假的解码器
  auto st = (AVStream *)meta.at("stream_object").value.ptr;
  return new FFMDecoder(st);
}

}
