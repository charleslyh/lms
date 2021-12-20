//
//  LMS.cpp
//  demo
//
//  Created by yuhuachli on 2021/12/6.
//

#include "LMS.h"
#include "Module.h"

namespace lms {

// TODO: 使用脚本进行注入，而不是手动导入符号
extern Module moduleLogger;
extern Module moduleRuntime;

static Module mods[] = {
  moduleRuntime,
};

void init() {
  moduleLogger.setup();
  LMSLogInfo("Module setup: %s", moduleLogger.name);
  
  for (auto& m : mods) {
    m.setup();
    LMSLogInfo("Module setup: %s", m.name);
  }
}

void unInit() {
  for (int i = sizeof(mods) / sizeof (mods[0]) - 1; i >= 0; --i) {
    auto& m = mods[i];

    LMSLogInfo("Module teardown: %s", m.name);
    m.teardown();
  }
  
  // 在logger模块teardown之前打印dump日志
  lms::dumpLeaks();

  LMSLogInfo("Module teardown: %s", moduleLogger.name);
  moduleLogger.teardown();
}

} // namespace lms
