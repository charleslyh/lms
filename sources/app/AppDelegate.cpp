#include <lms/Player.h>
#include <lms/MediaSource.h>
#include <lms/Logger.h>
#include <extension/sdl/SDLApplication.h>
#include <extension/sdl/SDLView.h>
extern "C" {
#include <libavformat/avformat.h>
}

class FFVideoFile : public lms::PassiveMediaSource {
public:
  FFVideoFile(const char *path) {
    LMSLogVerbose("path: %s", path);

    this->context = nullptr;
    this->path = strdup(path);
    this->queue = lms::createDispatchQueue("video_file");
  }

  ~FFVideoFile() {
    assert(context == nullptr);

    lms::release(this->queue);
    free(this->path);
  }

  int numberOfStreams() override {
    return context->nb_streams;
  }
  
  lms::StreamMeta streamMetaAt(int index) override {
    auto st = context->streams[index];
    return {
      "ffmpeg",
      st,
      index,
      (lms::MediaType)st->codecpar->codec_type,
      av_q2d(st->avg_frame_rate),
    };
  }

  int open() override {
    int rt = 0;

    rt = avformat_open_input(&context, path, nullptr, nullptr);
    if (rt != 0) {
      LMSLogError("Failed opening video file: %s", path);
      return rt;
    }

    rt = avformat_find_stream_info(context, nullptr);
    if (rt != 0) {
      LMSLogError("Failed finding stream info");
      return rt;
    }
    
    av_dump_format(context, 0, path, 0);
    return 0;
  }

  void close() override {
    avformat_close_input(&context);
  }

  void loadPackets(int numberRequested) override {
    LMSLogVerbose("loadPackets: requested=%d", numberRequested);
    
    class AVPacketHolder : public lms::ResourceHolder {
    protected:
      void* retain(void *object) override {
        return object;
      }
      
      void release(void *object) override {
        av_packet_unref((AVPacket *)object);
      }
    };
    
    for (int i = 0; i< numberRequested; i += 1) {
      // TODO: 使用独立的queue来加载数据
      dispatchAsync(lms::mainQueue(), [this] {
        AVPacket *avpkt = av_packet_alloc();
        int rt = av_read_frame(context, avpkt);
        if (rt >= 0) {
          LMSLogVerbose("AVPacket loaded: st=%d, flags=0x%-2x, dts=%" PRIu64
                        ", pts=%" PRIu64 ", dur=%" PRIu64 ", sz=%-6d",
                        avpkt->stream_index,
                        avpkt->flags,
                        avpkt->dts,
                        avpkt->pts,
                        avpkt->duration,
                        avpkt->size);  
          
          lms::ResourceHolder *holder = new AVPacketHolder;
          lms::Packet *pkt = new lms::Packet(avpkt, holder);
          pkt->streamIndex = avpkt->stream_index;
          pkt->data        = avpkt->data;
          pkt->size        = avpkt->size;
          pkt->pts         = avpkt->pts;

          deliverPacket(pkt);

          lms::release(pkt);
          lms::release(holder);
        }
      });
    }
  }

private:
  char *path;
  AVFormatContext    *context;
  lms::DispatchQueue *queue;
};


class PlayerAppDelegate: public SDLAppDelegate {
public:  
  void didFinishLaunchingApplication(int argc, char **argv) override {
    lms::init();
    lms::setLogLevel(lms::LogLevelVerbose);

    auto src = lms::autoRelease(new FFVideoFile(argv[1]));
    player = new lms::Player(src, lms::autoRelease(new SDLView));
    player->play();
  }

  void willTerminateApplication() override {
    player->stop();
    lms::release(player);
    lms::unInit();
  }

private:
  lms::Player *player;
};

int main(int argc, char *argv[]) {
  SDLApplication app(argc, argv);
  
  // 通过添加{}来明确delegate的释放时机，避免DumpLeaks误认为delegate为泄露资源
  PlayerAppDelegate delegate;
  app.run(&delegate);
  
  lms::dumpLeaks();
}
