#pragma once

#include "LMSFoundation/Foundation.h"
extern "C" {
  #include <libavformat/avformat.h>
}
#include <list>


namespace lms {

const int StreamIndexAny = -1;

class PacketSource;

class PacketAcceptor : virtual public Object {
public:
  virtual void didReceivePacket(Packet *pkt) = 0;
};


class PacketSource : virtual public Object {
public:
  virtual int open() = 0;
  virtual void close() = 0;
  virtual int numberOfStreams() = 0;
  virtual Metadata streamMetaAt(int streamIndex) = 0;

public:
  void addPacketAcceptor(int streamIndex, PacketAcceptor *acceptor);
  void removePacketAcceptor(PacketAcceptor *acceptor);

protected:
  void deliverPacket(void *pkt);

private:
  std::list<std::pair<int, PacketAcceptor *>> acceptors;
};


class PassivePacketSource : public PacketSource {
public:
  virtual void loadPackets(int numberRequested) = 0;
};

}
