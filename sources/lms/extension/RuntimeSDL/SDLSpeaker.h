//
//  SDLSpeaker.hpp
//  RuntimeSDL
//
//  Created by yuhuachli on 2022/1/14.
//

#pragma once
#include <lms/Foundation.h>
#include <lms/Cell.h>
#include <lms/TimeSync.h>
#include <lms/Buffer.h>
#include <lms/Events.h>
extern "C" {
#include <libavformat/avformat.h>
#include <SDL2/SDL.h>
}

namespace lms {

class SDLSpeaker: public Cell {
  typedef struct {
    AVFrame *frame;
    uint8_t *rptr;
    int      remainBytes;
  } AudioFrameItem;
  
public:
  SDLSpeaker(AVStream *stream, TimeSync *timeSync) {
    this->stream     = stream;
    this->timeSync   = lms::retain(timeSync);
    this->frameItems = new FramesBuffer<AudioFrameItem *>;
    this->totalSamples = 0;
    
    SDL_AudioSpec request_specs, respond_specs;
    request_specs.freq     = stream->codecpar->sample_rate;
    request_specs.format   = AUDIO_S16;
    request_specs.channels = av_get_channel_layout_nb_channels(stream->codecpar->channel_layout);
    request_specs.silence  = 0;

    // stream->codecpar->frame_size表示每个音频帧中包含的样本数，这么做可以尽可能保证每个数据回调都正好消耗掉一个音频帧
    request_specs.samples  = stream->codecpar->frame_size;
    request_specs.callback = (SDL_AudioCallback) loadAudioData;
    request_specs.userdata = this;

    speakerId = SDL_OpenAudioDevice(NULL,
                                    0,
                                    &request_specs,
                                    &respond_specs,
                                    SDL_AUDIO_ALLOW_FORMAT_CHANGE);

  }
  
  ~SDLSpeaker() {
    SDL_CloseAudioDevice(speakerId);

    lms::release(timeSync);
    lms::release(frameItems);
  }
  
protected:
  void didReceivePipelineMessage(const PipelineMessage& msg) override {
    auto avfrm = (AVFrame *)msg.at("frame").value.ptr;
    AudioFrameItem *afi = new AudioFrameItem { avfrm, avfrm->data[0], avfrm->linesize[0] };
    frameItems->pushBack(afi);
    
    totalSamples += avfrm->linesize[0];
  }
  
private:
  static void loadAudioData(SDLSpeaker *self, Uint8 *data, int len) {
    memset(data, 0, len);
    
    while(len > 0) {
      if (self->frameItems->count() < SDLSpeaker::IdealCachingFrames) {
        fireEvent("decode_frame", self, {
          {"stream_object", self->stream}
        });
      }
      
      AudioFrameItem *afi = self->frameItems->popFront();
      if (afi == nullptr) {
        LMSLogWarning("No audio frame!");
        break;
      }
      
      AVFrame *frame = afi->frame;

      double ts = frame->pts * av_q2d(self->stream->time_base);
      self->timeSync->updateTimePivot(ts);
      
      LMSLogVerbose("Rendering audio frame | ts:%.2lf, pts:%llu, remain_bytes:%u, remains_frames:%lu",
                    ts, frame->pts, afi->remainBytes, self->frameItems->count());
            
      int bytesToWrite = std::min(afi->remainBytes, len);
      memcpy(data, afi->rptr, bytesToWrite);
      
      len -= bytesToWrite;
      afi->remainBytes -= bytesToWrite;
      afi->rptr += bytesToWrite;
      
      self->totalSamples -= bytesToWrite;

      // 如果frame中剩余了数据未消费，则重新放入待处理队列
      if (afi->remainBytes > 0) {
        self->frameItems->pushFront(afi);
      } else {
        av_freep(&frame->data[0]);
        av_frame_free(&frame);
        delete afi;
      }
    }
  }

private:
  void start() override {
    SDL_PauseAudioDevice(speakerId, 0);
  }
  
  void stop() override {
    SDL_PauseAudioDevice(speakerId, 1);
  }
  
private:
  SDL_AudioDeviceID speakerId;
  
  AVStream *stream;
  TimeSync *timeSync;
  FramesBuffer<AudioFrameItem *> *frameItems;
  std::atomic<uint32_t> totalSamples;
  
  constexpr static int IdealCachingFrames = 10;
};


}
