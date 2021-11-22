//
//  Packet.cpp
//  lms
//
//  Created by yuhuachli on 2021/11/20.
//

#include "LMSFoundation/Packet.h"

namespace lms {

Packet::Packet(void *internalPacket, ResourceHolder *holder) {
  assert(internalPacket);
  assert(holder);
  
  this->holder = lms::retain(holder);
  this->internalPacket = holder->retain(internalPacket);
}

Packet::~Packet() {
  holder->release(internalPacket);
  lms::release(holder);
}

}
