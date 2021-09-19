#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>


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
  if (object != nullptr) {
    object->retain();
  }
  return object;
}

template<class T>
T autoRelease(T object) {
  performAutoRelease(object);
  return (T) object;
}

void drainAutoReleasePool();


class Runnable : virtual public Object {
public:
  virtual int run() = 0;
};


class DispatchQueue : virtual public Object {
public:
  virtual void async(Runnable *runnable) = 0;
  virtual void asyncPeriodically(int delayMs, Runnable *runnable) = 0;
};


typedef struct {
  DispatchQueue *dispatchQueue;
} InitParams;

void init(InitParams params);
void unInit();


typedef int (*ActionBlock)(void *context, void *data1, void *data2);

void dispatchAsync(DispatchQueue *queue, Runnable *runnable);
void dispatchAsync(DispatchQueue *queue, ActionBlock block, void *context, void *data1 = nullptr, void *data2 = nullptr);
void dispatchAsync(DispatchQueue *queue, std::function<int()> lambda);

void dispatchAsyncPeriodically(DispatchQueue *queue, int delayMS, std::function<int()> lambda);

DispatchQueue *mainQueue();

DispatchQueue *createDispatchQueue(const char *queueName);

typedef std::map<std::string, void*> Metadata;
typedef void Frame;
typedef void Packet;

}
