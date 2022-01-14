#include "Player.h"
#include "MediaSource.h"
#include "Decoder.h"
#include "Buffer.h"
#include "Logger.h"
#include "Runtime.h"
#include "Cell.h"
#include "Events.h"
#include "Stream.h"
#include "SourceDriver.h"
#include "VideoRenderDriver.h"
#include "TimeSync.h"
extern "C" {
  #include <libavcodec/avcodec.h>
  #include "libavutil/avutil.h"
  #include <libavutil/opt.h>
  #include <libavformat/avformat.h>
  #include <libswscale/swscale.h>
  #include <libswresample/swresample.h>
  #include <libavutil/imgutils.h>
  #include <SDL2/SDL.h>
}
#include <vector>
#include <algorithm>


namespace lms {

extern Cell *createSpeaker(AVStream* stream, TimeSync *tsync);
extern Cell *createAudioResampler(AVStream *stream);

constexpr double InvalidPlayingTime = -1.0;

Player::Player(MediaSource *mediaSource, Cell *vrender) {
  this->source = lms::retain(mediaSource);
  this->vrender     = lms::retain(vrender);
  this->coordinator = new SourceDriver(mediaSource);
  this->timesync    = new TimeSync;
  this->vstream     = nullptr;
  this->astream     = nullptr;
}

Player::~Player() {
  assert(!vstream);
  assert(!astream);

  lms::release(coordinator);
  lms::release(timesync);
  lms::release(source);
  lms::release(vrender);
}

void Player::play() {
  sync(hostQueue(), "StartPlay", [this] {
    doPlay();
  });
}

void Player::stop() {
  sync(hostQueue(), "StopPlay", [this] {
    doStop();
  });
}

void Player::doPlay() {
  LMSLogInfo(nullptr);

  // 必须先加载source的数据才能获取当中的元信息
  if (source->open() != 0) {
    return;
  }
  
  auto nbStreams = source->numberOfStreams();
  int  streamId = -1;
  for (int i = 0; i < nbStreams; i += 1) {
    auto meta   = source->getStreamMeta(i);
    auto mtype  = meta.at("media_type").value.u;
    auto stream = (AVStream *)meta.at("stream_object").value.ptr;

    if (mtype == MediaTypeVideo) {
      VideoRenderDriver *driver = new VideoRenderDriver(stream, vrender, timesync);
      
      Cell *decoder = createDecoder(meta);
      vstream = new Stream(meta, decoder, nullptr, driver);

      vrender->configure(meta);

      lms::release(driver);
      lms::release(decoder);
    } else if (mtype == MediaTypeAudio) {
      Cell *speaker = createSpeaker(stream, timesync);
      Cell *decoder = createDecoder(meta);
      Cell *resampler = createAudioResampler(stream);
      astream = new Stream(meta, decoder, resampler, speaker);
      
      lms::release(resampler);
      lms::release(decoder);
      lms::release(speaker);
    }
  }
  
  if (vstream == nullptr && astream == nullptr) {
    LMSLogError("Failed creating video & audio stream");
    return;
  }
  
  // [#55 避免视频的头几帧被丢弃]
  // 在player启动播放时，会立即开始帧渲染。但是视频的播放可能早于处理音频第一帧的时间。而播放时间轴的初始化是在音频首帧播放
  // 时设置的。这会导致部分部分视频帧被丢弃。所以，在astream, vstream启动前，应手动重置播放时间轴为无效状态。直到音频首帧加载后将时间轴
  // 置零为止。
  timesync->updateTimePivot(InvalidPlayingTime);
  
  if (vstream) {
    source->addReceiver(vstream);
    vstream->start();
  }
  
  if (astream) {
    source->addReceiver(astream);
    astream->start();
  }
  
  coordinator->start();
}

void Player::doStop() {
  LMSLogInfo(nullptr);
  
  coordinator->stop();
  
  if (astream) {
    astream->stop();
    source->removeReceiver(astream);
  }
  
  if (vstream) {
    vstream->stop();
    source->removeReceiver(vstream);
  }

  source->close();

  lms::release(vstream);
  vstream = nullptr;
  
  lms::release(astream);
  astream = nullptr;
}

} // namespace lms
