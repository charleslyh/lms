#include <cstdio>
#include <list>
#include <algorithm>
#include "LMSFoundation/Foundation.h"

namespace lms {

Object::Object() {
  refCount = 1;
}


Object::~Object() = default;


void Object::retain() {
  refCount += 1;

  printf("Object::retain(%p)\n", this);
}


void Object::release() {
  printf("Object::release(%p)\n", this);

  refCount -= 1;

  if (refCount <= 0) {
    delete this;
  }
}


void retain(Object *object) {
  object->retain();
}


void release(Object *object) {
  object->release();
}


class Core {
public:
  std::list<Object *> auto_release_pool;
};

static Core *core = nullptr;


void performAutoRelease(Object *object) {
  core->auto_release_pool.push_back(object);
}


void drainAutoReleasePool() {
  if (core->auto_release_pool.empty()) {
    return;
  }

  printf("> drainAutoReleasePool(): %lu\n", core->auto_release_pool.size());

  std::for_each(begin(core->auto_release_pool), end(core->auto_release_pool), [] (Object *obj) {
    obj->release();
  });

  core->auto_release_pool.clear();

  printf("< drainAutoReleasePool()\n");
}


void init() {
  core = new Core;
}


void unInit() {
  delete core;
}

}