#pragma once

#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Packet.h"
extern "C" {
  #include <libavformat/avformat.h>
}
#include <list>
#include <string>


namespace lms {

typedef int StreamId;

const StreamId StreamIdAny = -1;

enum MediaType {
  MediaTypeVideo = 0,
  MediaTypeAudio = 1,
};

struct StreamMeta {
  std::string format;  // 例如 ffmpeg
  void       *data;
  StreamId    streamId;
  MediaType   mediaType;
  double      averageFPS;
};

class MediaSource;
class Packet;

class PacketAcceptor : virtual public Object {
public:
  virtual void didReceivePacket(Packet *pkt) = 0;
};


class MediaSource : virtual public Object {
public:
  virtual int open() = 0;
  virtual void close() = 0;
  virtual int numberOfStreams() = 0;
  virtual StreamMeta streamMetaAt(int streamIndex) = 0;

public:
  void addPacketAcceptor(StreamId streamId, PacketAcceptor *acceptor);
  void removePacketAcceptor(PacketAcceptor *acceptor);

protected:
  void deliverPacket(Packet *packet);

private:
  std::list<std::pair<StreamId, PacketAcceptor *>> acceptors;
};


class PassiveMediaSource : public MediaSource {
public:
  virtual void loadPackets(int numberRequested) = 0;
};

class Demuxer : virtual public Object {
public:
  
};

}
