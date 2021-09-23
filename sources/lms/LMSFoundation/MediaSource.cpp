#include "LMSFoundation/MediaSource.h"

namespace lms {

void MediaSource::addPacketAcceptor(int streamIndex, PacketAcceptor *acceptor) {
  acceptors.push_back({streamIndex, acceptor});
}

void MediaSource::removePacketAcceptor(PacketAcceptor *acceptor) {
  acceptors.remove_if([acceptor] (std::pair<int, PacketAcceptor *> item) {
    return acceptor == item.second;
  });
}

void MediaSource::deliverPacket(Packet *pkt) {
  std::for_each(begin(acceptors), end(acceptors), [this, pkt] (const std::pair<int, PacketAcceptor *>& item) {
    auto avpkt = (AVPacket *)pkt;
    if (item.first == StreamIdAny || item.first == avpkt->stream_index) {
      item.second->didReceivePacket(pkt);
    }
  });
}

} // namespace lms
