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
  lms::release(videoFramesBuffer);
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
    this->source = lms::retain(source);
  }
  
  ~Coordinator() {
    lms::release(source);
  }
  
  /* from buffer */
  void didTouchFrames(size_t framesStored) override {
    int packetsToLoad = FramesExpected - (int)framesStored - packetsLoading - packetsDecoding;
    
    // 保底缓存帧数，由于加载是需要时间的，所以如果一帧来了再去请求下一帧，就会退化成串行处理模式
    // 应对策略是使用一个保底帧数来使加载、解码、渲染成为并行流水线模式
    packetsToLoad += 5;
    
    // 如果一个packet中包含较多的帧，则可能导致framesStored较大。进而使得packetsToLoad为负数
    // 如：初始状态下，发起15个packets加载请求
    // 每个packet中解出了3个frame。则framesStored为45, packetsLoading, packetsDecoding均为0
    // 最终，packetsToLoad 为 15 - 45 - 0 - 0 + 5 = -25
    // 对此，认为只要framesStored超过了FramesExpected，则为缓存充足的情况，可以不发起加载请求
    packetsToLoad = std::max(0, packetsToLoad);

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

  const int FramesExpected = 10;
  int packetsLoading = 0;
  int packetsDecoding = 0;
};

void Player::play() {
  LMSLogInfo(nullptr);

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
