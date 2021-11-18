#pragma once

#include "LMSFoundation/Stacktrace.h"
#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <list>
#include <set>
#include <algorithm>

#define LMS_TRACE_LEAKS_ENABLED 0

namespace lms {

class Object {
protected:
  Object();
  virtual ~Object();

private:
  void ref();
  void unref(bool postphone);
    
private:
  std::atomic<int> refCount;
  
  // 赋予lms级别的几个资源管理方法以访问权限，以便调用者可以用下面几个更加便利的方法来进行引用计数管理
  template<class T> friend T retain(T);
  template<class T> friend T autoRelease(T);
  friend void release(Object*);
};

template<class T>
T retain(T object) {
  if (object == nullptr) {
    return nullptr;
  }
  
  object->ref();
  return object;
}

inline void release(Object* object) {
  if (object == nullptr) {
    return;
  }
  
  object->unref(false);
  return object;
}

template<class T>
T autoRelease(T object) {
  if (object == nullptr) {
    return;
  }
  
  object->unref(true);
  return object;
}

void drainAutoReleasePool();

class Runnable : virtual public Object {
public:
  virtual void run() = 0;
};


class DispatchQueue : virtual public Object {
public:
  virtual void async(Runnable *runnable) = 0;
  virtual int asyncPeriodically(double period, Runnable *runnable) = 0;
  virtual void cancelPeriodicalJob(int jobId) = 0;
};


typedef struct {
  DispatchQueue *dispatchQueue;
} InitParams;

void init(InitParams params);
void unInit();


typedef int (*ActionBlock)(void *context, void *data1, void *data2);

void dispatchAsync(DispatchQueue *queue, Runnable *runnable);
void dispatchAsync(DispatchQueue *queue, ActionBlock block, void *context, void *data1 = nullptr, void *data2 = nullptr);
void dispatchAsync(DispatchQueue *queue, std::function<void()> lambda);

typedef int PeriodicJobId;

PeriodicJobId dispatchAsyncPeriodically(DispatchQueue *queue, double period, std::function<void()> lambda);
void cancelPeriodicObj(DispatchQueue *queue, PeriodicJobId jobId);

void stopPeriodicalQueues();

DispatchQueue *mainQueue();

DispatchQueue *createDispatchQueue(const char *queueName);

typedef std::map<std::string, void*> Metadata;
typedef void Frame;
typedef void Packet;

class FrameAcceptor : virtual public Object {
public:
  virtual void didReceiveFrame(Frame *frame) = 0;
};


class FrameSource : virtual public Object {
public:
  void addFrameAcceptor(FrameAcceptor *acceptor) {
    if (acceptor == nullptr) {
      return;
    }

    lms::retain(acceptor);
    acceptors.push_back(acceptor);
  }
  
  void removeFrameAcceptor(FrameAcceptor *acceptor) {
    acceptors.remove_if([acceptor] (FrameAcceptor *acc) {
      if (acc == acceptor) {
        lms::release(acc);
        return true;
      } else {
        return false;
      }
    });
  }
  
protected:
  void deliverFrame(Frame *frame) {
    for (auto acc : acceptors) {
      acc->didReceiveFrame(frame);
    };
  }
  
private:
  std::list<FrameAcceptor *> acceptors;
};


void dumpBytes(uint8_t *data, int size, int bytesPerLine);
void dumpLeaks();

}
