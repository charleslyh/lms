//
//  Cell.cpp
//  lms
//
//  Created by yuhuachli on 2021/12/16.
//

#include "Cell.h"

namespace lms {

void Cell::addReceiver(Cell *receiver) {
  // TODO: ASSERT(is lms main thread)
  receivers.push_back(receiver);
}

void Cell::removeReceiver(Cell *receiver) {
  // TODO: ASSERT(is lms main thread)
  receivers.remove(receiver);
}

void Cell::deliverCellMessage(const CellMessage& cmsg) {
  // TODO: ASSERT(is lms main thread)
  
  for (auto r : receivers) {
    r->didReceiveCellMessage(cmsg);
  }
}

}
