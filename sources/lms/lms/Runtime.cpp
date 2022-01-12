//
//  Runtime.cpp
//  lms
//
//  Created by yuhuachli on 2021/11/29.
//

#include "Module.h"
#include "Runtime.h"

namespace lms {

static DispatchQueue *_mainQueue;

DispatchQueue *mainQueue() {
  return _mainQueue;
}

bool isMainThread() {
  return _mainQueue->isHostThread();
}

void async(DispatchQueue *queue, void *sender, Runnable *runnable) {
  queue->async(sender, runnable);
}

void async(DispatchQueue *queue, void *sender, std::function<void()> action) {
  auto r = new LambdaRunnable(action);
  queue->async(sender, r);
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
  _mainQueue = createDispatchQueue("LMS Main");
}

static void teardownModuleRuntime() {
  release(_mainQueue);
  _mainQueue = nullptr;
}

Module moduleRuntime = {
  .name     = "Runtime",
  .setup    = setupModuleRuntime,
  .teardown = teardownModuleRuntime
};

}
