#include <cstdio>
#include "LMSFoundation/Player.h"

namespace lms {

Player::Player(PassivePacketSource *src) {
  printf("Player::Player(): %p\n", this);
  this->src = lms::retain(src);
}

Player::~Player() {
  lms::release(src);
  printf("Player::~Player(): %p\n", this);
}

void Player::play() {
  printf("Player::play()\n");

  class tester : public PacketAcceptor {
  public:
    tester(PassivePacketSource *src) : src(src) {
    }

    void onReceivePacket(void *packet) {
      AVPacket& sharedPacket = *((AVPacket *)packet);

      printf("Packet {stream:%d, size:%d, pts:%lld, dts:%lld}\n"
         , sharedPacket.stream_index
         , sharedPacket.size
         , sharedPacket.pts
         , sharedPacket.dts);

      src->loadPackets(1);
    }

  private:
    PassivePacketSource *src;
  };

  this->src->addAcceptor(new tester(src));
  this->src->open();
  this->src->loadPackets(1);
}

void Player::stop() {
  printf("Player::stop()\n");

  this->src->close();
}

}