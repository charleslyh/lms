//
//  Cell.hpp
//  lms
//
//  Created by yuhuachli on 2021/12/16.
//

#pragma once

#include <lms/Foundation.h>

namespace lms {

typedef std::map<std::string, Variant> PipelineMessage;

class Cell : virtual public Object {
public:
  virtual void configure(const StreamMeta& meta) {}
  virtual void start() = 0;
  virtual void stop() = 0;
  
  virtual void didReceivePipelineMessage(const PipelineMessage& cmsg) = 0;
  
public:
  void addReceiver(Cell *receiver);
  void removeReceiver(Cell *receiver);

protected:
  void deliverPipelineMessage(const PipelineMessage& cmsg);
  
private:
  std::list<Cell *> receivers;
};

}
