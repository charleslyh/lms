#pragma once

#include <atomic>
#include <cstdio>


namespace lms {

class Object {
protected:
  Object();
  virtual ~Object();

public:
  void retain();
  void release();

private:
  std::atomic<int> refCount;
};

void release(Object *object);
void performAutoRelease(Object *object);

template<class T>
T retain(T object) {
  object->retain();
  return object;
}

template<class T>
T autoRelease(T object) {
  performAutoRelease(object);
  return (T) object;
}

void flushAutoReleasePool();

}