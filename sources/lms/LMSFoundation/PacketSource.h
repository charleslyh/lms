#pragma once

#include "LMSFoundation/Foundation.h"
extern "C" {
#include <libavformat/avformat.h>
}
#include <algorithm>
#include <list>
#include <string>
#include <map>
#include <utility>


namespace lms {

const int StreamIndexAny = -1;

class PacketSource;

class PacketAcceptor : public Object {
public:
  virtual void didReceivePacket(void *pkt) = 0;
};


class PacketSource : public Object {
public:
  virtual int open() = 0;
  virtual void close() = 0;
  virtual int numberOfStreams() = 0;
  virtual Metadata streamMetaAt(int streamIndex) = 0;
  
public:
  void addPacketAcceptor(int streamIndex, PacketAcceptor *acceptor) {
    acceptors.push_back({streamIndex, acceptor});
  }

  void removePacketAcceptor(PacketAcceptor *acceptor) {
    acceptors.remove_if([acceptor] (std::pair<int, PacketAcceptor *> item) {
      return acceptor == item.second;
    });
  }

protected:
  void deliverPacket(void *pkt) {
    std::for_each(begin(acceptors), end(acceptors), [this, pkt] (std::pair<int, PacketAcceptor *>& item) {
      auto avpkt = (AVPacket *)pkt;
      if (item.first == StreamIndexAny || item.first == avpkt->stream_index) {
        item.second->didReceivePacket(pkt);
      }
    });
  }

private:
  std::list<std::pair<int, PacketAcceptor *>> acceptors;
};


class PassivePacketSource : public PacketSource {
public:
  virtual void loadPackets(int numberRequested) = 0;
};

}
