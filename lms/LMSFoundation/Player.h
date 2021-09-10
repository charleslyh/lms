#include <cstdio>
#include "LMSFoundation/Foundation.h"

namespace lms {

class VideoFile: public Object {
public:
  VideoFile(const char *path) {
    printf("VideoFile::VideoFile(\"%s\"): %p\n", path, this);
  }

  ~VideoFile() {
    printf("VideoFile::~VideoFile(): %p\n", this);
  }
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