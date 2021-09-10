#include <cstdio>
#include <list>
#include <algorithm>
#include "LMSFoundation/Foundation.h"

namespace lms {

Object::Object() {
  refCount = 1;
}

Object::~Object() {  
}

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


static std::list<Object *> _auto_release_pool;


void retain(Object *object) {
  object->retain();
}


void release(Object *object) {
  object->release();
}


void performAutoRelease(Object *object) {
  _auto_release_pool.push_back(object);
}


void flushAutoReleasePool() {
  if (_auto_release_pool.size() == 0) {
    return;
  }

  printf("> flushAutoReleasePool(): %lu\n", _auto_release_pool.size());

  std::for_each(begin(_auto_release_pool), end(_auto_release_pool), [] (Object *obj) {
    obj->release();
  });

  _auto_release_pool.clear();

  printf("< flushAutoReleasePool()\n");
}

}