//
//  Cell.cpp
//  lms
//
//  Created by yuhuachli on 2021/12/16.
//

#include "Cell.h"
#include "Runtime.h"

namespace lms {

void Cell::addReceiver(Cell *receiver) {
  // TODO: ASSERT(is lms main thread)
  receivers.push_back(receiver);
}

void Cell::removeReceiver(Cell *receiver) {
  // TODO: ASSERT(is lms main thread)
  receivers.remove(receiver);
}

void Cell::deliverPipelineMessage(const PipelineMessage& cmsg) {
  assert(isMainThread());
  
  for (auto r : receivers) {
    r->didReceivePipelineMessage(cmsg);
  }
}

}
