#include "MediaSource.h"
#include "Packet.h"

namespace lms {

void MediaSource::addPacketAcceptor(int streamIndex, PacketAcceptor *acceptor) {
  acceptors.push_back({streamIndex, acceptor});
}

void MediaSource::removePacketAcceptor(PacketAcceptor *acceptor) {
  acceptors.remove_if([acceptor] (std::pair<int, PacketAcceptor *> item) {
    return acceptor == item.second;
  });
}

void MediaSource::deliverPacket(Packet *packet) {
  for (auto item : acceptors) {
    if (item.first == StreamIdAny || item.first == packet->streamIndex) {
      item.second->didReceivePacket(packet);
    }
  };
}

} // namespace lms
