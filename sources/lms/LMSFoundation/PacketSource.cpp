#include "LMSFoundation/PacketSource.h"

namespace lms {

void PacketSource::addPacketAcceptor(int streamIndex, PacketAcceptor *acceptor) {
  acceptors.push_back({streamIndex, acceptor});
}

void PacketSource::removePacketAcceptor(PacketAcceptor *acceptor) {
  acceptors.remove_if([acceptor] (std::pair<int, PacketAcceptor *> item) {
    return acceptor == item.second;
  });
}

void PacketSource::deliverPacket(void *pkt) {
  std::for_each(begin(acceptors), end(acceptors), [this, pkt] (std::pair<int, PacketAcceptor *>& item) {
    auto avpkt = (AVPacket *)pkt;
    if (item.first == StreamIndexAny || item.first == avpkt->stream_index) {
      item.second->didReceivePacket(pkt);
    }
  });
}

} // namespace lms