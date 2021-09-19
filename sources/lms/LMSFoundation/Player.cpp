#include "LMSFoundation/Player.h"
#include "LMSFoundation/PacketSource.h"
#include "LMSFoundation/Decoder.h"
#include "LMSFoundation/Render.h"
extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
}
#include <cstdio>
#include <cmath>

namespace lms {

Player::Player(PassivePacketSource *s) {
  src = lms::retain(s);
  videoDecoder = nullptr;
  videoRender  = nullptr;
  videoFramesBuffer = nullptr;
}

Player::~Player() {
  lms::release(videoRender);
  lms::release(videoDecoder);
  lms::release(src);
}

void Player::setVideoRender(Render *render) {
  if (videoRender != nullptr) {
    lms::release(videoRender);
  }

  videoRender = lms::retain(render);
}

class Coordinator : public FramesBufferDelegate, public PacketAcceptor, public DecoderDelegate {
public:
  Coordinator(PassivePacketSource *source) {
    this->source = source;
  }
  
  /* from buffer */
  void didTouchFrames(size_t currentBufferingFrames) override {
    int packetsToLoad = std::max(0, idealBufferingFrames + 3 - (int)currentBufferingFrames - packetsLoading - packetsDecoding);
    if (packetsToLoad <= 0) {
      return;
    }

    packetsLoading += packetsToLoad;
    source->loadPackets(packetsToLoad);
  }
  
  /* from src */
  void didReceivePacket(void *packet) override {
    packetsLoading  -= 1;
  }

  /* from ecoder */
  void willStartDecodingPacket(void *packet) override {
    packetsDecoding += 1;
  }

  /* from decoder */
  void didFinishDecodingPacket(void *packet) override {
    packetsDecoding -= 1;
  }
  
private:
  PassivePacketSource *source;

  const int idealBufferingFrames = 10;
  int packetsLoading = 0;
  int packetsDecoding = 0;
};

void Player::play() {
  LMSLogInfo("Start playing");

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
      break;
    }
  }

  if (videoDecoder == nullptr) {
    return;
  }
  
  
  auto coordinator = new Coordinator(src);
  autoRelease((Object *)coordinator);
  
  src->addPacketAcceptor(StreamIndexAny, coordinator);
  videoDecoder->setDelegate(coordinator);
  
  videoFramesBuffer = new FramesBuffer;
  videoFramesBuffer->setDelegate(coordinator);
  videoDecoder->addFrameAcceptor(videoFramesBuffer);

  videoDecoder->startDecoding();
  videoRender->startRendering(videoDecoder->meta(), videoFramesBuffer);
}

void Player::stop() {
  LMSLogInfo("Stop playing");

  src->removePacketAcceptor(videoDecoder);

  videoDecoder->stopDecoding();
  src->close();
}

} // namespace lms
