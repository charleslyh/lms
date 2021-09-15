#include <cstdio>
#include "LMSFoundation/Player.h"

namespace lms {

Player::Player(PassivePacketSource *src) {
  printf("Player::Player(): %p\n", this);
  this->src = lms::retain(src);
  this->dec = nullptr;
}

Player::~Player() {
  lms::release(this->dec);
  lms::release(this->src);
  printf("Player::~Player(): %p\n", this);
}

void Player::play() {
  printf("Player::play()\n");

  if (this->src->open() != 0) {
    return;
  }

  Decoder *decoder = lms::createDecoder(this->src);
  if (decoder == nullptr) {
    return;
  }

  this->dec = decoder;
  this->dec->open();
  this->src->addAcceptor(this->dec);

  this->src->loadPackets(1);
}

void Player::stop() {
  printf("Player::stop()\n");

  this->src->removeAcceptor(this->dec);

  this->dec->close();
  this->src->close();
}

}
