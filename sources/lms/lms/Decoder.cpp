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
    
    frameDecoded = av_frame_alloc();
  }
  
protected:
  void start() override;
  void stop() override;
  
  void didReceivePipelineMessage(const PipelineMessage& msg) override;
  
private:
  void tryDecodeFrame() {
    AVFrame frame = {0};

    int rt = 0;
    do {
      rt = avcodec_receive_frame(codecContext, &frame);
      if (rt == AVERROR_EOF) {
        // 已读取完所有数据
        break;
      } else if (rt == AVERROR(EAGAIN)) {
        AVPacket *avpkt = nullptr;
        SDL_LockMutex(packetsMutex);
        {
          if (!packets.empty()) {
            avpkt = packets.front();
            packets.pop_front();
            
            LMSLogVerbose("Packtes remain: count=%u", (uint32_t)packets.size());
          }
        }
        SDL_UnlockMutex(packetsMutex);

        if (avpkt) {
          rt = avcodec_send_packet(codecContext, avpkt);
          av_packet_free(&avpkt);
        } else {
          rt = AVERROR_EOF;
        }

        if (rt == 0) {
          rt = AVERROR(EAGAIN);
        } else if (rt != 0) {
          break;
        }
      } else if (rt < 0) {
        LMSLogError("Error while decoding: %d", rt);
        break;
      }
    } while (rt != 0);
    
    if (rt == 0) {
      uint32_t now = SDL_GetTicks();
      LMSLogDebug("*CP* Frame decoded: pts=%" PRIi64, frame.pts);
      
      PipelineMessage frameMsg;
      frameMsg["type"]  = "media_frame";
      frameMsg["frame"] = &frame;
      deliverPipelineMessage(frameMsg);
      av_frame_unref(&frame);
    }
  }
  
  static int decodingThreadProc(FFMDecoder *self) {
    while(true) {
      SDL_SemWait(self->sem);
      if (!self->decoding) {
        break;
      }
      
      self->tryDecodeFrame();
    }
    
    return 0;
  }

private:
  AVStream *stream;
  AVCodecParameters *params;
  AVCodecContext *codecContext;
  AVCodec *codec;
  AVFrame *frameDecoded;
  
  std::list<AVPacket *> packets;
  SDL_mutex            *packetsMutex;
  SDL_sem              *sem;
  SDL_Thread           *thread;
  bool                 decoding;
};

void FFMDecoder::start() {
  LMSLogInfo("decoder=%p", this);
  
  thread       = nullptr;
  packetsMutex = SDL_CreateMutex();
  sem          = SDL_CreateSemaphore(0);

  int rt = avcodec_open2(codecContext, codec, 0);
  if (rt != 0) {
    LMSLogError("Couldn't open codec: %d", rt);
    return;
  }
  
//  if (params->codec_type == AVMEDIA_TYPE_VIDEO) {
    addEventObserver("shouldLoadNextFrame", nullptr, [this] (const char *name, void *sender, const EventParams& p) {
      AVStream *streamObject = (AVStream *)variantsGetPointer(p, "stream_object");
      
      // 仅当发起方为对应流时，才应该激活解码处理，否则可能会提前解码数据帧
      if (streamObject == this->stream) {
        SDL_SemPost(this->sem);
      }
    });
    
    const char *tname = params->codec_type == AVMEDIA_TYPE_VIDEO ? "DecodingVideo" : "DecodingAudio";
    decoding = true;
    thread   = SDL_CreateThread((SDL_ThreadFunction)FFMDecoder::decodingThreadProc, tname, this);
//  }
}

void FFMDecoder::stop() {
  LMSLogInfo("decoder=%p", this);
  
  if (thread) {
    decoding = false;
    SDL_SemPost(sem);
    SDL_WaitThread(thread, nullptr);
  }
   
  SDL_DestroySemaphore(sem);

  // TODO: 完整的流程应该是
  // TODO: 1. 置状态为stopping
  // TODO: 2. didReceivePacket拒处理新的数据包（也许应该由上层来保证？例如先拆除掉上游节点）
  // TODO: 3. drain queue中所有的解码 blocks，且解码block应该正确判断状态，仅当running的时候才可以解码
  // TODO: 4. 置状态未stopped
  // TODO: 5. 释放其它资源
  lms::mainQueue()->cancel(this);

  avcodec_close(codecContext);
  
  SDL_DestroyMutex(packetsMutex);
}

void FFMDecoder::didReceivePipelineMessage(const PipelineMessage& msg) {
  auto srcpkt = (AVPacket *)msg.at("packet_object").value.ptr;
  assert(srcpkt != nullptr);

//  if (params->codec_type == AVMEDIA_TYPE_AUDIO) {
//    AVPacket *avpkt = av_packet_clone(srcpkt);
//    std::shared_ptr<AVPacket> pktHolder(avpkt, [] (AVPacket *pkt) { av_packet_free(&pkt); });
//
//    // TODO: 使用独立的queue来进行解码
//    async(mainQueue(), this, [this, avpkt, pktHolder]() {
//      LMSLogDebug("Begin decode: stream=%d, flags=0x%02X, duration=%" PRIi64 ", pts=%" PRIi64,
//                  avpkt->stream_index, avpkt->flags, avpkt->duration, avpkt->pts);
//
//      int rt = avcodec_send_packet(codecContext, avpkt);
//      if (rt < 0) {
//        LMSLogError("Error sending packet for decoding: %d", rt);
//      }
//
//      AVFrame *frame = frameDecoded;
//      while (rt >= 0) {
//        rt = avcodec_receive_frame(codecContext, frame);
//        if (rt == AVERROR(EAGAIN)) {
//          // 数据不足，需要继续喂数据才能完成解码
//          break;
//        } else if (rt == AVERROR_EOF) {
//          // 已读取完所有数据
//          break;
//        } else if (rt < 0) {
//          LMSLogError("Error while decoding: %d", rt);
//          break;
//        }
//
//        PipelineMessage frameMsg;
//        frameMsg["type"]  = "media_frame";
//        frameMsg["frame"] = frame;
//        deliverPipelineMessage(frameMsg);
//        av_frame_unref(frame);
//      }
//
//      LMSLogDebug("End decode: stream=%d, pts=%" PRIi64, avpkt->stream_index, avpkt->pts);
//    });
//  } else if (params->codec_type == AVMEDIA_TYPE_VIDEO) {
    SDL_LockMutex(packetsMutex);
    {
      // 由于下面的解码过程可能会被异步调度（延迟）处理，为了避免在函数‘立即’返回后，pkt资源被释放
      // 这里对pkt进行了一次人为引用（或者需要clone？）
      AVPacket *avpkt = av_packet_clone(srcpkt);
      packets.push_back(avpkt);
    }
    SDL_UnlockMutex(packetsMutex);
//  }
}

Cell *createDecoder(const StreamMeta& meta) {
  // 根据meta信息匹配一个可创建，且最合适的解码器

  // 为了测试，返回一个假的解码器
  auto st = (AVStream *)meta.at("stream_object").value.ptr;
  return new FFMDecoder(st);
}

}
