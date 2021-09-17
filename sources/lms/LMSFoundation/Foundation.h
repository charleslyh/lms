#pragma once

#include <atomic>
#include <cstdio>
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
  object->retain();
  return object;
}

template<class T>
T autoRelease(T object) {
  performAutoRelease(object);
  return (T) object;
}

void drainAutoReleasePool();


class Runnable : public Object {
public:
  virtual void run() = 0;
};


class DispatchQueue : public Object {
public:
  virtual void async(Runnable *runnable) = 0;
  virtual void schedule(Runnable *runnable, int delayMs) = 0;
};


typedef struct {
  DispatchQueue *dispatchQueue;
} InitParams;

void init(InitParams params);
void unInit();


typedef void (*ActionBlock)(void *context, void *data1, void *data2);

void dispatchAsync(DispatchQueue *queue, Runnable *runnable);
void dispatchAsync(DispatchQueue *queue, ActionBlock block, void *context, void *data1 = nullptr, void *data2 = nullptr);
void dispatchAsync(DispatchQueue *queue, std::function<void()> lambda);

void dispatchAfter(DispatchQueue *queue, int delayMS, std::function<void()> lambda);

DispatchQueue *mainQueue();

DispatchQueue *createDispatchQueue(const char *queueName);

typedef std::map<std::string, void*> Metadata;
typedef void Frame;
typedef void Packet;
}
