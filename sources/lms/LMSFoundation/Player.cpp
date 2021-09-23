#include "LMSFoundation/Player.h"
#include "LMSFoundation/MediaSource.h"
#include "LMSFoundation/Decoder.h"
#include "LMSFoundation/Render.h"
#include "LMSFoundation/Buffer.h"
#include "LMSFoundation/Logger.h"
extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
}

namespace lms {

class VideoStream : public PacketAcceptor, public DecoderDelegate {
public:
  VideoStream(PassiveMediaSource *source, StreamMeta meta, Render *render) {
    this->meta        = meta;
    this->source      = lms::retain(source);
    this->decoder     = lms::createDecoder(meta);
    this->render      = lms::retain(render);
    this->buffer      = new FramesBuffer;
  }
  
  ~VideoStream() {
    lms::release(source);
    lms::release(decoder);
    lms::release(buffer);
    lms::release(render);
  }

  void start() {
    /*
     依赖关系：
     *source -> *codec -> buffer -> *render
        ^          ^         ^
        ----- coordinator ----
     */
    source->addPacketAcceptor(meta.streamId, decoder);
    decoder->addFrameAcceptor(buffer);
    buffer->addFrameAcceptor(render);

    render->start(decoder->meta());
    decoder->start();
    
    source->addPacketAcceptor(StreamIdAny, this);
    decoder->setDelegate(this);
    
    lms::dispatchAsyncPeriodically(mainQueue(), meta.avg_fps, [this] {
      drive();
    });
  }
  
  void stop() {
    decoder->stop();
    render->stop();

    buffer->removeFrameAcceptor(render);
    decoder->removeFrameAcceptor(buffer);
    source->removePacketAcceptor(decoder);
  }
  
private:
  void drive() {
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
  void didReceivePacket(Packet *packet) override  {
    packetsLoading  -= 1;
  }
  
  /* from ecoder */
  void willStartDecodingPacket(Packet *packet) override {
    packetsDecoding += 1;
  }
  
  /* from decoder */
  void didFinishDecodingPacket(Packet *packet) override {
    packetsDecoding -= 1;
  }
  
private:
  StreamMeta meta;
  PassiveMediaSource *source;
  Decoder            *decoder;
  FramesBuffer       *buffer;
  Render             *render;
  
  const int FramesExpected = 10;
  int packetsLoading = 0;
  int packetsDecoding = 0;
};

Player::Player(PassiveMediaSource *s, Render *vrender) {
  this->source  = lms::retain(s);
  this->vrender = lms::retain(vrender);
  this->vstream = nullptr;
}

Player::~Player() {
  lms::release(vstream);
  lms::release(source);
}

void Player::play() {
  LMSLogInfo(nullptr);

  /* 必须先加载source的数据才能获取当中的元信息 */
  if (source->open() != 0) {
    return;
  }
  
  auto nbStreams = source->numberOfStreams();
  int  streamId = -1;
  for (int i = 0; i < nbStreams; i += 1) {
    auto meta = source->streamMetaAt(i);
    if (meta.mediaType == MediaTypeVideo) {
      vstream = new VideoStream(source, meta, vrender);
    }
  }
  
  if (vstream == nullptr) {
    LMSLogError("Failed creating video stream");
    return;
  }
  
  vstream->start();
}

void Player::stop() {
  LMSLogInfo(nullptr);
  
  if (vstream) {
    vstream->stop();
  }
  
  source->close();
}

} // namespace lms
