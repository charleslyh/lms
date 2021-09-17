#include "LMSFoundation/Player.h"
extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
}
#include <cstdio>
#include <cmath>

namespace lms {

Player::Player(PassivePacketSource *s) {
  printf("Player::Player(): %p\n", this);
  src = lms::retain(s);
  videoDecoder = nullptr;
  videoRender  = nullptr;
  videoFramesBuffer = nullptr;
}

Player::~Player() {
  lms::release(videoRender);
  lms::release(videoDecoder);
  lms::release(src);
  printf("Player::~Player(): %p\n", this);
}

void Player::setVideoRender(VideoRender *render) {
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
  
  void didTouchFrames(size_t currentBufferingFrames) override {
    int packetsToLoad = std::max(0, idealBufferingFrames + 5 - (int)currentBufferingFrames - packetsLoading - packetsDecoding);
    printf("CORD: toLoad:%2d <= cbf:%d, loading:%d, decoding:%d\n"
           ,packetsToLoad
           ,(int)currentBufferingFrames
           ,packetsLoading
           ,packetsDecoding);

    if (packetsToLoad <= 0) {
      return;
    }
    
    packetsLoading += packetsToLoad;
    source->loadPackets(packetsToLoad);
  }
  
  /* from src */
  void didReceivePacket(void *packet) override {
    printf("CORD: didReceivePacket\n");
    packetsLoading  -= 1;
  }
   
  void willStartDecodingPacket(void *packet) override {
    packetsDecoding += 1;
  }

  /* from decoder */
  void didFinishDecodingPacket(void *packet) override {
    printf("CORD: didFinishDecodingPacket\n");
    packetsDecoding -= 1;
  }
  
private:
  PassivePacketSource *source;

  const int idealBufferingFrames = 1;
  int packetsLoading = 0;
  int packetsDecoding = 0;
};

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
      break;
    }
  }

  if (videoDecoder == nullptr) {
    return;
  }
  
  
  auto coordinator = new Coordinator(src);
//  lms::autoRelease((Object *)coordinator);
  
  src->addPacketAcceptor(StreamIndexAny, coordinator);
  videoDecoder->setDelegate(coordinator);
  
  videoFramesBuffer = new FramesBuffer;
  videoFramesBuffer->setDelegate(coordinator);
  videoDecoder->addFrameAcceptor(videoFramesBuffer);

  videoDecoder->startDecoding();
  videoRender->startRendering(videoDecoder->meta(), videoFramesBuffer);
}

void Player::stop() {
  printf("Player::stop()\n");

  src->removePacketAcceptor(videoDecoder);

  videoDecoder->stopDecoding();
  src->close();
}

} // namespace lms
