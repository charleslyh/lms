#pragma once

#include <lms/Foundation.h>
#include <lms/Cell.h>
#include <list>

namespace lms {

typedef int StreamId;

const StreamId StreamIdAny = -1;

enum MediaType {
  MediaTypeVideo = 0,
  MediaTypeAudio = 1,
};

class MediaSource : public Object {
public:
  virtual int open() = 0;
  virtual void close() = 0;

  virtual int numberOfStreams() = 0;
  virtual StreamMeta getStreamMeta(size_t streamIndex) = 0;

  virtual void loadPackets(int numberRequested) = 0;

public:
  void addReceiver(Cell *receiver);
  void removeReceiver(Cell *receiver);

protected:
  void deliverPacketMessage(const PipelineMessage& msg);

private:
  std::list<Cell *> receivers;
};

}
