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

void Player::play() {
  printf("Player::play()\n");
}

void Player::stop() {
  printf("Player::stop()\n");    
}

}