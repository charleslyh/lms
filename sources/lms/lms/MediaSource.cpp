#include "MediaSource.h"

namespace lms {

void MediaSource::addReceiver(Cell *receiver) {
  receivers.push_back(receiver);
}

void MediaSource::removeReceiver(Cell *receiver) {
  receivers.remove(receiver);
}

void MediaSource::deliverPacketMessage(const PipelineMessage& msg) {
  for (auto r : receivers) {
    r->didReceivePipelineMessage(msg);
  };
}

} // namespace lms
