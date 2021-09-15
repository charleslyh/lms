#pragma once

#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/PacketSource.h"
#include "LMSFoundation/Decoder.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <cstdio>
#include <list>

namespace lms {


class VideoFile : public PassivePacketSource {
public:
  VideoFile(const char *path) {
    printf("VideoFile::VideoFile(\"%s\"): %p\n", path, this);
    this->path  = strdup(path);
    this->queue = createDispatchQueue("video_file");
  }

  ~VideoFile() {
    lms::release(this->queue);
    free(this->path);
    printf("VideoFile::~VideoFile(): %p\n", this);
  }

  std::map<std::string, void*> metadata() override {
    return {};
  }

  int open() override {
    context = nullptr;

    int rt = 0;

    rt = avformat_open_input(&context, path, nullptr, nullptr);
    if (rt != 0) {
      printf("Failed opening video file: %s\n", path);
      return rt;
    }

    rt = avformat_find_stream_info(context, nullptr);
    if (rt != 0) {
      printf("Failed finding stream info in video file!\n");
      return rt;
    }
    
    av_dump_format(context, 0, path, 0);
    return 0;
  }

  void close() override {
    avformat_close_input(&context);
  }

  void loadPackets(int numberRequested) override {
    dispatchAsync(mainQueue(), [this] () {
      int status = av_read_frame(context, &sharedPacket);
    
      if (status >= 0) {
        deliverPacket(&sharedPacket);
        av_packet_unref(&sharedPacket);
      }
    });    
  }

private:
  char *path;
  AVFormatContext *context;
  AVPacket sharedPacket;

  DispatchQueue *queue;
};


class Player: public Object {
public:
  Player(PassivePacketSource *src);
  ~Player();

  void play();
  void stop();

private:
  PassivePacketSource *src;
  Decoder             *dec;
};

}
