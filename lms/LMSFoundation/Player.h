#include "LMSFoundation/Foundation.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <cstdio>
#include <string>
#include <list>

namespace lms {

class PacketAcceptor {
public:
  virtual void onReceivePacket(void *packet) = 0;
};

class PassivePacketSource : public Object {
public:
  virtual void open() = 0;
  virtual void close() = 0;
  virtual void loadPackets(int numberRequested) = 0;

  void addAcceptor(PacketAcceptor *acceptor) {
    acceptors.push_back(acceptor);
  }

  void removeAcceptor(PacketAcceptor *acceptor) {
    acceptors.remove(acceptor);    
  }

protected:
  void deliverPacket(void *packet) {
    std::for_each(begin(acceptors), end(acceptors), [packet] (PacketAcceptor *acceptor) {
      acceptor->onReceivePacket(packet);
    });
  }

private:
  std::list<PacketAcceptor *> acceptors;
};

class VideoFile : public PassivePacketSource {
public:
  VideoFile(const char *path) {
    printf("VideoFile::VideoFile(\"%s\"): %p\n", path, this);
    this->path = strdup(path);
  }

  ~VideoFile() {
    free(this->path);
    printf("VideoFile::~VideoFile(): %p\n", this);
  }

  void open() override {
    context = nullptr;
    int rt = 0;

    rt = avformat_open_input(&context, path, nullptr, nullptr);
    if (rt != 0) {
      printf("Failed opening video file!\n");
      return;
    }

    rt = avformat_find_stream_info(context, nullptr);
    if (rt != 0) {
      printf("Failed finding stream info in video file!\n");
      return;
    }
    
    av_dump_format(context, 0, path, 0);
  }

  void close() override {
    avformat_close_input(&context);
  }

  void loadPackets(int numberRequested) override {
    dispatchAsync([this] () {
      int status = av_read_frame(context, &sharedPacket);
    
      if (status >= 0) {
        deliverPacket(&sharedPacket);
        av_packet_unref(&sharedPacket);
      }
    });    
  }

private:
  AVFormatContext *context;
  AVPacket sharedPacket;
  char *path;
};


class Player: public Object {
public:
  Player(PassivePacketSource *src);
  ~Player();

  void play();
  void stop();

private:
  PassivePacketSource *src;
};

}