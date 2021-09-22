#include "LMSFoundation/Player.h"
#include "LMSFoundation/PacketSource.h"
#include "LMSFoundation/Decoder.h"
#include "LMSFoundation/Render.h"
#include "LMSFoundation/Buffer.h"
#include "LMSFoundation/Logger.h"
extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
}
#include <cstdio>
#include <cmath>

namespace lms {

Player::Player(PassivePacketSource *s) {
  source = lms::retain(s);
  decoder = nullptr;
  render  = nullptr;
  buffer = nullptr;
}

Player::~Player() {
  lms::release(buffer);
  lms::release(render);
  lms::release(decoder);
  lms::release(source);
}

void Player::setVideoRender(Render *render) {
  if (render != nullptr) {
    lms::release(render);
  }

  render = lms::retain(render);
}

class Coordinator : public PacketAcceptor, public DecoderDelegate {
public:
  Coordinator(PassivePacketSource *source, Decoder *decoder, FramesBuffer *buffer) {
    this->source  = lms::retain(source);
    this->buffer  = lms::retain(buffer);
    this->decoder = lms::retain(decoder);
  }
  
  ~Coordinator() {
    lms::release(buffer);
    lms::release(decoder);
    lms::release(source);
  }
  
  void start() {
    source->addPacketAcceptor(StreamIndexAny, this);
    decoder->setDelegate(this);
    
    jobId = lms::dispatchAsyncPeriodically(mainQueue(), 33, [this] {
      driveStreaming();
    });
  }
  
  void stop() {
    lms::cancelPeriodicObj(jobId);
    
    decoder->setDelegate(nullptr);
    source->removePacketAcceptor(this);
  }
  
private:
  void driveStreaming() {
    buffer->squeezeFrame(0);
    
    int packetsToLoad = FramesExpected - buffer->numberOfFrames() - packetsLoading - packetsDecoding;
    
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
  
protected:
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
  Decoder             *decoder;
  FramesBuffer        *buffer;
  int jobId;

  const int FramesExpected = 10;
  int packetsLoading = 0;
  int packetsDecoding = 0;
};

void Player::play() {
  LMSLogInfo(nullptr);

  if (source->open() != 0) {
    return;
  }
  
  auto nbStreams = source->numberOfStreams();
  int  streamIndex = -1;
  for (int i = 0; i < nbStreams; i += 1) {
    auto meta = source->streamMetaAt(i);
    auto st   = (AVStream *)meta["stream"];
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      streamIndex = i;
      decoder = lms::createDecoder(meta);
      break;
    }
  }

  if (decoder == nullptr) {
    return;
  }
  
  buffer      = new FramesBuffer;
  coordinator = new Coordinator(source, decoder, buffer);

  source->addPacketAcceptor(streamIndex, decoder);
  decoder->addFrameAcceptor(buffer);
  buffer->addFrameAcceptor(render);

  decoder->start();
  render->start(decoder->meta());
  coordinator->start();
}

void Player::stop() {
  LMSLogInfo("Stop playing");
  
  coordinator->stop();
  decoder->stop();
  source->close();
  
  source->removePacketAcceptor(decoder);
}

} // namespace lms
