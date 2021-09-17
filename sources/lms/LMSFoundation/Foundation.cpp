#include <cstdio>
#include <list>
#include <algorithm>
#include <cassert>
#include "LMSFoundation/Foundation.h"

namespace lms {

Object::Object()
:refCount(1)
{
}


Object::~Object() = default;


void Object::retain() {
  refCount += 1;

  // printf("Object::retain(%p)\n", this);
}


void Object::release() {
  // printf("Object::release(%p)\n", this);

  refCount -= 1;

  if (refCount == 0) {
    delete this;
  }
}


void retain(Object *object) {
  if (object == nullptr) {
    return;
  }

  object->retain();
}


void release(Object *object) {
  if (object == nullptr) {
    return;
  }

  object->release();
}


class Core {
public:
  explicit Core(DispatchQueue *mainQueue) {
    this->mainQueue = retain(mainQueue);
  }

  ~Core() {
    release(this->mainQueue);
  }

public:
  std::list<Object *> autoReleasePool;
  DispatchQueue       *mainQueue;
};

static Core *core = nullptr;


void performAutoRelease(Object *object) {
  core->autoReleasePool.push_back(object);
}


void drainAutoReleasePool() {
  if (core->autoReleasePool.empty()) {
    return;
  }

  // printf("> drainAutoReleasePool(): %lu\n", core->autoReleasePool.size());

  std::for_each(begin(core->autoReleasePool), end(core->autoReleasePool), [] (Object *obj) {
    obj->release();
  });

  core->autoReleasePool.clear();

  // printf("< drainAutoReleasePool()\n");
}


void init(InitParams params) {
  assert(params.dispatchQueue != nullptr);

  core = new Core(params.dispatchQueue);
}


void unInit() {
  delete core;
}


DispatchQueue *mainQueue() {
  return core->mainQueue;
}

DispatchQueue *createDispatchQueue(const char *queueName) {
  return retain(core->mainQueue);
}

void dispatchAsync(DispatchQueue *queue, Runnable *runnable) {
  queue->async(runnable);
}

void dispatchAsync(DispatchQueue *queue, ActionBlock block, void *context, void *data1, void *data2) {
  class ActionBlockRunnable : public Runnable {
  public:
    ActionBlockRunnable(ActionBlock block, void *context, void *data1, void *data2)
    : block(block) {
      this->context = context;
      this->data1   = data1;
      this->data2   = data2;
    }

    void run() override {
      block(context, data1, data2);
    }

  private:
    ActionBlock block;
    void *context;
    void *data1;
    void *data2;
  };

  queue->async(autoRelease(new ActionBlockRunnable(block, context, data1, data2)));
}

class LambdaRunnable : public Runnable {
public:
  LambdaRunnable(std::function<void()> l)
  : lambda(l) {
  }

  void run() override {
    lambda();
  }

private:
  std::function<void()> lambda;
};

void dispatchAsync(DispatchQueue *queue, std::function<void()> lambda) {
  queue->async(autoRelease(new LambdaRunnable(lambda)));
}

void dispatchAfter(DispatchQueue *queue, int delayMS, std::function<void()> lambda) {
  queue->schedule(autoRelease(new LambdaRunnable(lambda)), delayMS);
}

}
