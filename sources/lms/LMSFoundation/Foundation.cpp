#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Logger.h"
#include <cstdio>
#include <list>
#include <algorithm>
#include <cassert>
#include <sstream>

extern "C" {
#include <SDL2/SDL.h>
}

namespace lms {

#if (LMS_TRACE_LEAKS_ENABLED)
class LeaksTracer {
  std::set<Object *> objects;
  SDL_mutex *mtx;
  
public:
  LeaksTracer() {
    mtx = SDL_CreateMutex();
  }
  
  ~LeaksTracer() {
    SDL_DestroyMutex(mtx);
  }
  
  void add(Object *obj) {
    SDL_LockMutex(mtx);
    objects.insert(obj);
    SDL_UnlockMutex(mtx);
  }
  
  void remove(Object *obj) {
    SDL_LockMutex(mtx);
    objects.erase(obj);
    SDL_UnlockMutex(mtx);
  }

  void dumpLeaks() {
    LMSLogVerbose("Leaks: %d", (int)objects.size());
    
    if (!objects.empty()) {
      LMSLogVerbose("--- Start ---");
      for (auto obj = objects.begin(); obj != objects.end(); ++obj) {
        LMSLogVerbose("obj: %p", *obj);
        Object *objptr = *obj;
        for (auto s = objptr->traces.begin(); s != objptr->traces.end(); ++s) {
          LMSLogVerbose("  %s", s->c_str());
        }
      }
      LMSLogVerbose("--- End ---");
    }
  }
};

static LeaksTracer __leaksTracer;
#  define TraceObject(obj)   __leaksTracer.add(obj)
#  define UntraceObject(obj) __leaksTracer.remove(obj)
#  define DumpLeaks()        __leaksTracer.dumpLeaks()
#else
#  define TraceObject(obj)
#  define UntraceObject(obj)
#  define DumpLeaks()
#endif // LMS_TRACE_LEAKS_ENABLED

void dumpLeaks() {
  DumpLeaks();
}

Object::Object() :refCount(1) {
#if (LMS_TRACE_LEAKS_ENABLED)
  const char *backtrace = stacktrace_caller_frame_desc("N");
  traces.push_back(backtrace);
  free((void *)backtrace);
#endif
  
  TraceObject(this);
}

Object::~Object() {
  UntraceObject(this);
}

void Object::ref() {
  refCount += 1;
}

void Object::unref() {
  int newCount = (refCount -= 1);
  
  if (newCount < 0) {
    assert(false);
  }

  if (newCount == 0) {
    delete this;
  }
}

void release(Object *object) {
  if (object == nullptr) {
    return;
  }

#if (LMS_TRACE_LEAKS_ENABLED)
  const char *backtrace = stacktrace_caller_frame_desc("U");
  object->traces.push_back(backtrace);
  free((void *)backtrace);
#endif

  object->unref();
}

class Core {
public:
  explicit Core(DispatchQueue *mainQueue) {
    this->arpMtx    = SDL_CreateMutex();
    this->mainQueue = lms::retain(mainQueue);
  }

  ~Core() {
    release(this->mainQueue);
    SDL_DestroyMutex(arpMtx);
  }

public:
  std::list<Object *> autoReleasePool;
  SDL_mutex *arpMtx;
  
  DispatchQueue       *mainQueue;
};

static Core *core = nullptr;


void autoReleaseObj(Object *object) {
  if (object == nullptr) {
    return;
  }

  core->autoReleasePool.push_back(object);
}


void drainAutoReleasePool() {
  if (core->autoReleasePool.empty()) {
    return;
  }
 
  for (auto it = core->autoReleasePool.begin(); it != core->autoReleasePool.end(); it++) {
    lms::release(*it);
  }

  core->autoReleasePool.clear();
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
  return lms::retain(core->mainQueue);
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
  auto r = new LambdaRunnable(lambda);
  queue->async(r);
  lms::release(r);
}

PeriodicJobId dispatchAsyncPeriodically(DispatchQueue *queue, double period, std::function<void()> lambda) {
  auto r = new LambdaRunnable(lambda);
  PeriodicJobId jobId = queue->asyncPeriodically(period, r);
  lms::release(r);
  return jobId;
}

void cancelPeriodicObj(DispatchQueue *queue, PeriodicJobId jobId) {
  queue->cancelPeriodicalJob(jobId);
}

void dumpBytes(uint8_t *data, int size, int bytesPerLine) {
  for (int i = 0; i < size; ++i) {
    printf("%02X ", data[i]);
    if ((i + 1) % bytesPerLine == 0) {
      printf("\n");
    }
  }

  if (size % bytesPerLine != 0) {
    printf("\n");
  }
}

}
