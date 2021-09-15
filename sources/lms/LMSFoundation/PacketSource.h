#pragma once

#include "LMSFoundation/Foundation.h"
#include <algorithm>
#include <list>
#include <string>
#include <map>


namespace lms {

class PacketSource : public Object {
public:
  virtual std::map<std::string, void*> metadata() = 0;
  virtual int open() = 0;
  virtual void close() = 0;
};


class PacketAcceptor : public Object {
public:
  virtual void onReceivePacket(PacketSource *src, void *pkt) = 0;
};


class PassivePacketSource : public PacketSource {
public:
  virtual void loadPackets(int numberRequested) = 0;

  void addAcceptor(PacketAcceptor *acceptor) {
    acceptors.push_back(acceptor);
  }

  void removeAcceptor(PacketAcceptor *acceptor) {
    acceptors.remove(acceptor);    
  }

protected:
  void deliverPacket(void *pkt) {
    std::for_each(begin(acceptors), end(acceptors), [this, pkt] (PacketAcceptor *acceptor) {
      acceptor->onReceivePacket(this, pkt);
    });
  }

private:
  std::list<PacketAcceptor *> acceptors;
};

}
