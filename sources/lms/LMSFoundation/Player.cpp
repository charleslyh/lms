#include "LMSFoundation/Player.h"
extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
}
#include <cstdio>

namespace lms {

Player::Player(PassivePacketSource *s) {
  printf("Player::Player(): %p\n", this);
  src = lms::retain(s);
  videoDecoder = nullptr;
  videoRender  = nullptr;
}

Player::~Player() {
  lms::release(videoRender);
  lms::release(videoDecoder);
  lms::release(src);
  printf("Player::~Player(): %p\n", this);
}

void Player::setVideoRender(Render *render) {
  if (videoRender != nullptr) {
    lms::release(videoRender);
  }

  videoRender = lms::retain(render);
}

void Player::play() {
  printf("Player::play()\n");

  if (src->open() != 0) {
    return;
  }
  
  auto nbStreams = src->numberOfStreams();
  for (int i = 0; i < nbStreams; i += 1) {
    auto meta = src->streamMetaAt(i);
    auto st   = (AVStream *)meta["stream"];
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoDecoder = lms::createDecoder(meta);
      src->addPacketAcceptor(i, videoDecoder);
      videoDecoder->addFrameAcceptor(videoRender);
      break;
    }
  }

  if (videoDecoder == nullptr) {
    return;
  }

  videoDecoder->prepare();
  videoRender->prepare(videoDecoder->meta());
  
  // 在最后向数据源增加一个主动加载触发器，以便在处理完成后循环加载下一个数据包
  src->addPacketAcceptor(StreamIndexAny, this);

  src->loadPackets(1);
}

void Player::stop() {
  printf("Player::stop()\n");

  src->removePacketAcceptor(videoDecoder);

  videoDecoder->teardown();
  src->close();
}

void Player::didReceivePacket(void *pkt) {
  src->loadPackets(1);
}

} // namespace lms
