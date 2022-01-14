//
//  SourceDriver.cpp
//  lms
//
//  Created by yuhuachli on 2022/1/14.
//

#include "SourceDriver.h"

namespace lms {

SourceDriver::SourceDriver(MediaSource *src) {
  source = lms::retain(src);
}

SourceDriver::~SourceDriver() {
  lms::release(source);
}

void SourceDriver::start() {
  LMSLogInfo("Start SourceDriver");

  eoDUP = addEventObserver("did_update_packets", nullptr, this, (EventCallback)onEventDidUpdatePackets);
  
  // 预加载一定数量的数据包
  fireEvent("load_packets", this, {
    { "count", 100 }
  });
}

void SourceDriver::stop() {
  LMSLogInfo("Stop SourceDriver");
  
  removeEventObserver(eoDUP);
  eoDUP = nullptr;
}

void SourceDriver::onEventDidUpdatePackets(SourceDriver *self, const char *ename, void *sender, const EventParams& p) {
  int type  = variantsGetUInt(p, "type");
  int dec   = variantsGetUInt(p, "decrement");
  
  // 解码器消费了多少个数据包，就重新加载多少个数据包。以此保证在链路上的缓存数据包总数是可控的
  if (type == 2 /* decrement */ && dec > 0) {
    fireEvent("load_packets", self, {{ "count", dec }});
  }
}

}
