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

/*
 @function createDispatchQueue
 创建一个DispatchQueue实例
 
 @param name 队列名称，如果队列会创建一个新线程，则该线程会使用该名称作为线程名

 @discuss
 需要在外部扩展模块中实现该方法，并链如主程序
*/
extern DispatchQueue *createDispatchQueue(const char *name);

DispatchQueue *mainQueue() {
  return _mainQueue;
}

bool isMainThread() {
  return _mainQueue->isMainThread();
}

void async(DispatchQueue *queue, void *sender, Runnable *runnable) {
  queue->enqueue(sender, runnable);
}

void async(DispatchQueue *queue, void *sender, std::function<void()> action) {
  auto r = new LambdaRunnable(action);
  queue->enqueue(sender, r);
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
