#pragma once

#include <lms/Foundation.h>
#include <lms/Packet.h>
extern "C" {
  #include <libavformat/avformat.h>
}
#include <list>
#include <string>
#include <map>


namespace lms {

typedef int StreamId;

const StreamId StreamIdAny = -1;

enum MediaType {
  MediaTypeVideo = 0,
  MediaTypeAudio = 1,
};

struct Variant {
  // 's' - std::string
  // '*' - void *
  char type;
  union {
    void *ptr;
    const char *cstr;
  } value;
  
  Variant(const char *cstr) {
    type = 's';
    value.cstr = strdup(cstr);
  }
  
  Variant(void *ptr) {
    type = '*';
    value.ptr = ptr;
  }
  
  ~Variant() {
    switch(type) {
      case 's':
        free((void *)value.cstr);
        break;
      case '*':
        break;
    }
  }
};

typedef std::map<std::string, Variant> StreamContext;

struct StreamMeta {
  std::string format;  // 例如 ffmpeg
  void       *data;
  StreamId    streamId;
  MediaType   mediaType;
  double      averageFPS;
  
  std::map<std::string, Variant> dict;
};

class MediaSource;
class Packet;

class PacketAcceptor : virtual public Object {
public:
  virtual void didReceivePacket(Packet *pkt) = 0;
};

class PacketSource : virtual public Object {
public:
  virtual void start() = 0;
  virtual void stop() = 0;
  
public:
  void addPacketAcceptor(PacketAcceptor *acceptor);
  void removePacketAcceptor(PacketAcceptor *acceptor);

protected:
  void deliverPacket(Packet *packet);

private:
  std::list<PacketAcceptor *> acceptors;
};


class MediaSource : virtual public Object {
public:
  virtual int open() = 0;
  virtual void close() = 0;
  virtual int numberOfStreams() = 0;
  virtual StreamMeta streamMetaAt(int streamIndex) = 0;
  
  virtual const StreamContext& streamContext() {}

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
