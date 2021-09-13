#include <cstdio>
#include "LMSFoundation/Player.h"

namespace lms {

Player::Player(VideoFile *src) {
  printf("Player::Player(): %p\n", this);
  this->src = lms::retain(src);
}

Player::~Player() {
  lms::release(src);
  printf("Player::~Player(): %p\n", this);
}

class DumpingMessage : public Runnable {
public:
  DumpingMessage() {
    printf("DumpingMessage::DumpingMessage()\n");
  }

  ~DumpingMessage() {
    printf("DumpingMessage::~DumpingMessage()\n");
  }

  void run() override {
    printf("DumpingMessage::run()\n");
  }
};

static void dumpingMessage(void *context, void *data1, void *data2) {
  printf("dumpingMessage(context: %p, data1: %p, data2: %p)\n", context, data1, data2);
}


void Player::play() {
  printf("Player::play()\n");

  lms::dispatchAsync(autoRelease(new DumpingMessage));
  lms::dispatchAsync(dumpingMessage, (void *)1, (void *)2, (void *)3);
}

void Player::stop() {
  printf("Player::stop()\n");    
}

}