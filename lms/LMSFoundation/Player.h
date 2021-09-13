#include "LMSFoundation/Foundation.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <cstdio>
#include <string>

namespace lms {

class StreamSource {
public:
  virtual void open() = 0;
  virtual void close() = 0;
};

class VideoFile: public Object, public StreamSource {
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

private:
  AVFormatContext *context;
  char *path;
};


class Player: public Object {
public:
  Player(VideoFile *src);
  ~Player();

  void play();
  void stop();

private:
  VideoFile *src;
};

}