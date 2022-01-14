//
//  VideoRenderDriver.cpp
//  lms
//
//  Created by yuhuachli on 2022/1/14.
//

#include "VideoRenderDriver.h"
#include "Cell.h"
#include "TimeSync.h"
#include "Runtime.h"
#include "Events.h"
#include "Logger.h"
extern "C" {
#include <libavformat/avformat.h>
#include <SDL2/SDL.h>
}

namespace lms {

VideoRenderDriver::VideoRenderDriver(AVStream *stream, Cell *videoRender, TimeSync *timeSync) {
  this->stream     = stream;
  this->render     = lms::retain(videoRender);
  this->timeSync   = lms::retain(timeSync);
  this->frameMutex = SDL_CreateMutex();
  this->nextFrame  = nullptr;
}

VideoRenderDriver::~VideoRenderDriver() {
  SDL_DestroyMutex(frameMutex);
  lms::release(render);
  lms::release(timeSync);
}

void VideoRenderDriver::start() {
  assert(isHostThread());
  
  q = createDispatchQueue("LMS_VRDriver", QueueTypeHost);
  nextFrame = nullptr;

  render->start();
  
  double fps = av_q2d(stream->avg_frame_rate);
  double spf = 1.0 / fps; // second per frame, also timer interval
  
  fpsTimer = scheduleTimer("LMS_VRDriver", spf, [this, spf] {
    assert(!isHostThread());

    double playingTime = timeSync->getPlayingTime();
    if (playingTime < 0) {
      return;
    }
    
    AVFrame *frame;
    
    EventParams loadingParams = {
      {"stream_object", this->stream}
    };
    
    while(true) {
      frame = nullptr;
      SDL_LockMutex(frameMutex);
      {
        if (nextFrame) {
          frame = nextFrame;
          nextFrame = nullptr;
        }
      }
      SDL_UnlockMutex(frameMutex);
      
      if (frame == nullptr) {
        lms::fireEvent("decode_frame", this, loadingParams);
        LMSLogWarning("No video frame!");
        return;
      }
      
      double frameTime = frame->best_effort_timestamp * av_q2d(stream->time_base);

      // deviation > 0 表示当前视频帧的应播时间大于当前播放时间（待播帧）
      // deviation < 0 表示当前视频帧的应播时间小于当前播放时间（迟滞帧），超过一定时间（tollerance）则认为是过期帧
      double deviation = frameTime - playingTime;

      LMSLogVerbose("Video frame popped | pts:%lld, ftime:%.2lf, ptime:%.2lf, dev:%.3lf(%.2lf frames)",
                    frame->pts, frameTime, playingTime, deviation, deviation / spf);

      double tollerance = spf / 2.0;
      if (deviation < -tollerance) {
        // 丢弃过期帧，继续下一帧（如果有）的处理
        LMSLogWarning("Video frame dropped");
        av_frame_unref(frame);

        lms::fireEvent("decode_frame", this, loadingParams);
        continue;
      } else
      if (deviation > tollerance) {
        SDL_LockMutex(frameMutex);
        {
          nextFrame = frame;
        }
        SDL_UnlockMutex(frameMutex);

        // 如果队列头的帧都未到播放时间，应认为后续帧也肯定未到播放时间，所以应直接退出渲染流程
        LMSLogWarning("Video frame refilled");

        frame = nullptr;
        return;
      } else {
        lms::fireEvent("decode_frame", this, loadingParams);
        break;
      }
    }
    
    assert(frame != nullptr);
    
    if (render) {
      std::shared_ptr<AVFrame> guard(frame, [] (AVFrame *frm) { av_frame_unref(frm); });
      async(q, "DeliverFrame", [this, frame, guard] {
        PipelineMessage msg;
        msg["type"]  = "media_frame";
        msg["frame"] = frame;
        render->didReceivePipelineMessage(msg);
      });
    }

  });
}

void VideoRenderDriver::stop() {
  assert(isHostThread());

  invalidateTimer(fpsTimer);
  lms::release(fpsTimer);
  
  render->stop();
  
  // TODO: 释放缓存帧
  nextFrame = nullptr;
  
  lms:release(q);
  q= nullptr;
}

void VideoRenderDriver::didReceivePipelineMessage(const PipelineMessage& msg) {
  auto avfrm = (AVFrame *)msg.at("frame").value.ptr;
  
  SDL_LockMutex(frameMutex);
  {
    if (nextFrame) {
      av_frame_free(&nextFrame);
    }

    nextFrame = av_frame_clone(avfrm);
  }
  SDL_UnlockMutex(frameMutex);
}

}
