//
//  Runtime.cpp
//  lms
//
//  Created by yuhuachli on 2021/11/29.
//

#include "Module.h"
#include "Runtime.h"

namespace lms {

static DispatchQueue *_hostQueue;

DispatchQueue *hostQueue() {
  return _hostQueue;
}

bool isHostThread() {
  return _hostQueue->isHostThread();
}

void async(DispatchQueue *queue, Runnable *runnable) {
  queue->async(runnable);
}

void async(DispatchQueue *queue, std::function<void()> action) {
  auto r = new LambdaRunnable(action);
  queue->async(r);
  lms::release(r);
}

void sync(DispatchQueue *queue, Runnable *r) {
  queue->sync(r);
}

void sync(DispatchQueue *queue, std::function<void()> action) {
  auto r = new LambdaRunnable(action);
  queue->sync(r);
  lms::release(r);
}

static void setupModuleRuntime() {
  // 创建LMS的主队列，该队列负责管理LMS的核心状态
  _hostQueue = createDispatchQueue("LMS_Host", QueueTypeHost);
}

static void teardownModuleRuntime() {
  release(_hostQueue);
  _hostQueue = nullptr;
}

Module moduleRuntime = {
  .name     = "Runtime",
  .setup    = setupModuleRuntime,
  .teardown = teardownModuleRuntime
};

}
