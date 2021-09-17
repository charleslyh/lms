#pragma once

#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/PacketSource.h"
#include "LMSFoundation/Decoder.h"
#include "LMSFoundation/Render.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <cstdio>
#include <list>
#include <cassert>

namespace lms {


class VideoFile : public PassivePacketSource {
public:
  VideoFile(const char *path) {
    printf("VideoFile::VideoFile(\"%s\"): %p\n", path, this);

    this->context = nullptr;
    this->path    = strdup(path);
    this->queue   = createDispatchQueue("video_file");
  }

  ~VideoFile() {
    printf("VideoFile::~VideoFile(): %p\n", this);
    
    assert(context == nullptr);

    lms::release(this->queue);
    free(this->path);
  }

  int numberOfStreams() override {
    return context->nb_streams;
  }
  
  Metadata streamMetaAt(int index) override {
    return {
      {"type",   (void *)"ffmpeg"},
      {"stream", context->streams[index]},
    };
  }

  int open() override {
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
    dispatchAsync(mainQueue(), [this, numberRequested] () {
      int numberRemains = numberRequested;
      while(numberRemains > 0) {
        AVPacket *packet = av_packet_alloc();
            
        int rt = av_read_frame(context, packet);
        if (rt >= 0) {
          deliverPacket(packet);
        }
        
        numberRemains -= 1;
      }
    });
  }

private:
  char *path;
  AVFormatContext *context;
  DispatchQueue   *queue;
};


class Player : public Object {
public:
  Player(PassivePacketSource *src);
  ~Player();
  
  void play();
  void stop();
  void setVideoRender(VideoRender *videoRender);

private:
  PassivePacketSource *src;
  Decoder      *videoDecoder;
  FramesBuffer *videoFramesBuffer;
  VideoRender  *videoRender;
};

}
