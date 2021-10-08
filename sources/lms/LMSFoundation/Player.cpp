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

class RenderDriver;

class RenderDriverDelegate : virtual public Object {
public:
  virtual void willRunRenderLoop(RenderDriver *driver) {}
  virtual void didRunRenderLoop(RenderDriver *driver, bool didRenderFrame) {}
};

class RenderDriver : public FramesBuffer {
public:
  virtual void start(const DecoderMeta& meta) = 0;
  virtual void stop() = 0;
  
  void setDelegate(RenderDriverDelegate *delegate) {
    lms::release(this->delegate);
    this->delegate = lms::retain(delegate);
  }
  
protected:
  RenderDriverDelegate *delegate = nullptr;
};

class VideoRenderDriver : public RenderDriver {
public:
  VideoRenderDriver(int maxBufferingFrames, Render *videoRender) {
    setIdealBufferingFrames(maxBufferingFrames);
    this->render = videoRender;
  }
  
  void start(const DecoderMeta& meta) override {
    addFrameAcceptor(render);
    
    render->start(meta);
    
    lms::dispatchAsyncPeriodically(mainQueue(), meta.fps, [this] {
      if (delegate) {
        delegate->willRunRenderLoop(this);
      }

      bool frameDeliveried = squeezeFrame(0);

      if (delegate) {
        delegate->didRunRenderLoop(this, frameDeliveried);
      }
    });
  }
  
  void stop() override {
    LMSLogError("TODO: Cancel periodic job");
    
    render->stop();

    removeFrameAcceptor(render);
  }
 
private:
  Render *render;
};

class Stream : virtual public Object {
public:
  Stream(const StreamMeta& meta, PassiveMediaSource *source, Decoder *decoder, RenderDriver *renderDriver) {
    this->meta         = meta;
    this->source       = lms::retain(source);
    this->renderDriver = lms::retain(renderDriver);
    this->decoder      = lms::retain(decoder);
  }
  
  ~Stream() {
    lms::release(source);
    lms::release(decoder);
    lms::release(renderDriver);
  }
  
  void start() {
    /*
     依赖关系：source -> codec -> renderDriver
                ^        ^        ^
                └──── vstream ────┘
     */
    source->addPacketAcceptor(meta.streamId, decoder);
    decoder->addFrameAcceptor(renderDriver);

    renderDriver->start(decoder->meta());
    decoder->start();
  }
  
  void stop() {
    decoder->stop();
    renderDriver->stop();

    decoder->removeFrameAcceptor(renderDriver);
    source->removePacketAcceptor(decoder);
  }
  
private:
  StreamMeta meta;
  PassiveMediaSource *source;
  Decoder            *decoder;
  RenderDriver       *renderDriver;
};

class Coordinator : public PacketAcceptor, public DecoderDelegate, public RenderDriverDelegate {
public:
  Coordinator(PassiveMediaSource *source) {
    this->source = lms::retain(source);
  }
  
  ~Coordinator() {
    lms::release(source);
  }

public:
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
  
  void didRunRenderLoop(RenderDriver* driver, bool frameRendered) override {
    int packetsToLoad = driver->numberOfEmptySlots() - packetsLoading - packetsDecoding;
    
    // 增加保底缓存帧数，避免当IdealFramesCount过小时（如1），一帧渲染了才去请求下一帧，从而退化为串行处理模式。
    // 应对策略是使用一个保底帧数来尽可能平衡数据加载、解码的速度刚好和渲染的消耗速度相匹配，从而成为并行流水线模式。
    // 如果该调整值过大，会导致缓存的帧数始终大于RenderDriver的理想缓存帧数。
    // 否则，如果该调整值过小，则无法解决退化为穿行处理模式的问题
    const int NumberOfFramesOffsetForLoadingCost = 5;
    packetsToLoad += NumberOfFramesOffsetForLoadingCost;
    
    // 如果一个packet中包含较多的帧，则可能导致framesCached较大。进而使得packetsToLoad为负数
    // 如：初始状态下，发起15个packets加载请求
    // 每个packet中解出了3个frame。则framesCached为45, packetsLoading, packetsDecoding均为0
    // 最终，packetsToLoad 为 15 - 45 - 0 - 0 + 5 = -25
    // 对此，认为只要framesCached超过了FramesExpected，则为缓存充足的情况，可以不发起加载请求
    packetsToLoad = std::max(0, packetsToLoad);

    if (packetsToLoad <= 0) {
      return;
    }

    packetsLoading += packetsToLoad;
    source->loadPackets(packetsToLoad);
  }
  
private:
  PassiveMediaSource *source;
  int packetsLoading = 0;
  int packetsDecoding = 0;
};


Player::Player(PassiveMediaSource *s, Render *vrender) {
  this->source  = lms::retain(s);
  this->vrender = lms::retain(vrender);
  this->vstream = nullptr;
  this->coordinator = lms::retain(new Coordinator(s));
}

Player::~Player() {
  lms::release(coordinator);
  lms::release(vstream);
  lms::release(source);
}

void Player::play() {
  LMSLogInfo(nullptr);

  source->addPacketAcceptor(StreamIdAny, coordinator);

  /* 必须先加载source的数据才能获取当中的元信息 */
  if (source->open() != 0) {
    return;
  }
  
  auto nbStreams = source->numberOfStreams();
  int  streamId = -1;
  for (int i = 0; i < nbStreams; i += 1) {
    auto meta = source->streamMetaAt(i);
    if (meta.mediaType == MediaTypeVideo) {
      VideoRenderDriver *driver = lms::autoRelease(new VideoRenderDriver(10, vrender));
      driver->setDelegate(coordinator);

      Decoder *decoder = lms::autoRelease(lms::createDecoder(meta));
      decoder->setDelegate(coordinator);

      vstream = new Stream(meta, source, decoder, driver);
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

  source->removePacketAcceptor(coordinator);

  lms::release(vstream);
  lms::release(coordinator);
}

} // namespace lms
