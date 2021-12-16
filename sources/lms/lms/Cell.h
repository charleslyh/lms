//
//  Cell.hpp
//  lms
//
//  Created by yuhuachli on 2021/12/16.
//

#pragma once

#include <lms/Foundation.h>

namespace lms {

typedef std::map<std::string, std::shared_ptr<Object>> CellMessage;

class Cell : virtual public Object {
public:
  virtual void configure(const StreamMetaInfo& streamMeta) = 0;
  virtual void start() = 0;
  virtual void stop() = 0;
  
  virtual void didReceiveCellMessage(const CellMessage& cmsg) = 0;
  
public:
  void addReceiver(Cell *receiver);
  void removeReceiver(Cell *receiver);

protected:
  void deliverCellMessage(const CellMessage& cmsg);
  
private:
  std::list<Cell *> receivers;
};

}
