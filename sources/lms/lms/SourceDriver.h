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
  SourceDriver(MediaSource *source) {
    this->source = lms::retain(source);
  }
  
  ~SourceDriver() {
    lms::release(source);
  }

public:
  void start() {
    LMSLogInfo("Start SourceDriver");

    eoDUP = addEventObserver("did_update_packets", nullptr, this, (EventCallback)onEventDidUpdatePackets);
    
    // 预加载一定数量的数据包
    fireEvent("load_packets", this, {
      { "count", 100 }
    });
  }
  
  void stop() {
    LMSLogInfo("Stop SourceDriver");
    
    removeEventObserver(eoDUP);
    eoDUP = nullptr;
  }
  
private:
  static void onEventDidUpdatePackets(SourceDriver *self, const char *ename, void *sender, const EventParams& p) {
    int type  = variantsGetUInt(p, "type");
    int dec   = variantsGetUInt(p, "decrement");
    
    if (type == 2 /* decrement */ && dec > 0) {
      fireEvent("load_packets", self, {{ "count", dec }});
    }
  }

private:
  MediaSource *source;
  void        *eoDUP;
};

}
