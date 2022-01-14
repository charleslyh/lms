//
//  SourceDriver.hpp
//  lms
//
//  Created by yuhuachli on 2022/1/14.
//

#pragma once
#include "Foundation.h"
#include "MediaSource.h"
#include "Logger.h"
#include "Events.h"

namespace lms {

class SourceDriver : virtual public Object {
public:
  SourceDriver(MediaSource *src);
  ~SourceDriver();

public:
  void start();
  void stop();
  
private:
  static void onEventDidUpdatePackets(SourceDriver *self, const char *ename, void *sender, const EventParams& p);

private:
  MediaSource *source;
  void        *eoDUP;
};

}
