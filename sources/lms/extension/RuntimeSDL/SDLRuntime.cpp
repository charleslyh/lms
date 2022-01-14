#include "SDLApplication.h"
#include <lms/Runtime.h>
#include <lms/Logger.h>
extern "C" {
#include <SDL2/SDL.h>
}
#include <cmath>

static Uint32 RunnableEvent;
static SDL_threadID _sdlMainThreadId;

class Synchronizer : public lms::Runnable {
public:
  Synchronizer(lms::Runnable *r) : Runnable(r->name()) {
    this->r   = lms::retain(r);
    this->sem = SDL_CreateSemaphore(0);
  }
  
  ~Synchronizer() {
    lms::release(r);
    SDL_DestroySemaphore(sem);
  }
  
  void wait() {
    SDL_SemWait(sem);
  }

  void run() override {
    r->run();
    SDL_SemPost(sem);
  }
  
  SDL_semaphore *sem;
  lms::Runnable *r;
};

class SDLWorkerQueue : public lms::DispatchQueue {
public:
  SDLWorkerQueue(const std::string& nm) {
    name = nm;
    mtx  = SDL_CreateMutex();
    sem  = SDL_CreateSemaphore(0);
    isRunning = true;
    thread = SDL_CreateThread((SDL_ThreadFunction)runloop, nm.c_str(), this);
  }
  
  ~SDLWorkerQueue() {
    isRunning = false;
    SDL_SemPost(sem);
    SDL_WaitThread(thread, nullptr);

    SDL_DestroySemaphore(sem);
    SDL_DestroyMutex(mtx);
  }
  
  bool isHostThread() override {
    return false;
  }
  
  void async(lms::Runnable *r) override {
    LMSLogDebug("Enqueue runnable: q=%s, t=worker/async, r=%s(%p)", name.c_str(), r->name(), r);
    r->enqueueTS = SDL_GetTicks();
    
    SDL_LockMutex(mtx);
    {
      lms::retain(r);
      runnables.push_back(r);
    }
    SDL_UnlockMutex(mtx);
    
    SDL_SemPost(sem);
  }
  
  void sync(lms::Runnable *r) override {
    LMSLogDebug("Enqueue runnable: q=%s, t=worker/sync , r=%s(%p)", name.c_str(), r->name(), r);
    r->enqueueTS = SDL_GetTicks();
    
    auto s = new Synchronizer(r);
    async(s);
    s->wait();
    lms::release(s);
  }
  
  void cancel() override {
    SDL_LockMutex(mtx);
    {
      for (auto r : runnables) {
        LMSLogDebug("Cancel runnable: q=%s, t=worker, r=%s(%p)", name.c_str(), r->name(), r);
        lms::release(r);
      }
      
      runnables = {};
    }
    SDL_UnlockMutex(mtx);
  }
  
private:
  static int runloop(SDLWorkerQueue *q) {
    while(q->isRunning) {
      SDL_SemWait(q->sem);
      if (!q->isRunning) {
        break;
      }
      
      lms::Runnable *r = nullptr;
      SDL_LockMutex(q->mtx);
      {
        if (!q->runnables.empty()) {
          r = q->runnables.front();
          q->runnables.pop_front();
        }
      }
      SDL_UnlockMutex(q->mtx);

      uint32_t now = SDL_GetTicks();
      uint32_t delay = now - r->enqueueTS;

      r->run();

      uint32_t cost = SDL_GetTicks() - now;
      LMSLogDebug("Launch runnalbe: q=%s, r=%s(%p), d=%-3d, c=%d", q->name.c_str(), r->name(), r, delay, cost);

      lms::release(r);
    }
    
    return 0;
  }
  
private:
  std::string name;
  bool isRunning;
  SDL_Thread *thread;
  SDL_mutex  *mtx;
  SDL_sem    *sem;
  std::list<lms::Runnable *> runnables;
};

class SDLHostQueue : public lms::DispatchQueue {
public:
  SDLHostQueue(const std::string& nm) {
    name = nm;
    mtx  = SDL_CreateMutex();
  }
  
  ~SDLHostQueue() {
    assert(lms::isHostThread());
    cancel();
  }
  
  bool isHostThread() override {
    SDL_threadID tid = SDL_ThreadID();
    return tid == _sdlMainThreadId;
  }
  
  void async(lms::Runnable *r) override {
    LMSLogDebug("Enqueue runnable: q=%s, t=async, r=%s(%p)", name.c_str(), r->name(), r);
    
    r->enqueueTS = SDL_GetTicks();
    
    lms::retain(r);

    SDL_LockMutex(mtx);
    runnables.push_back(r);
    SDL_UnlockMutex(mtx);
    
    SDL_Event event;
    SDL_zero(event);
    event.type       = RunnableEvent;
    event.user.data1 = this;
    event.user.code  = 0;
    SDL_PushEvent(&event);
  }
  
  void sync(lms::Runnable *r) override {
    LMSLogDebug("Enqueue runnable: q=%s, t=sync , r=%s(%p)", name.c_str(), r->name(), r);

    r->enqueueTS = SDL_GetTicks();

    if (isHostThread()) {
      // items的消费过程可能会产生新的runnable，所以不能直接在items队列中进行消费
  
      std::list<lms::Runnable *> cpy;
      SDL_LockMutex(mtx);
      {
        cpy = runnables;
        runnables = {};
      }
      SDL_UnlockMutex(mtx);
      
      for (auto ahead : cpy) {
        launch(ahead, true);
      }

      launch(r, false);
    } else {
      auto sync = new Synchronizer(r);
      async(sync);
      sync->wait();
      lms::release(sync);
    }
  }
  
  void launch(lms::Runnable *r, bool autoRelease) {
    uint32_t now = SDL_GetTicks();
    uint32_t delay = now - r->enqueueTS;

    r->run();

    uint32_t cost = SDL_GetTicks() - now;
    LMSLogDebug("Launch runnalbe: q=%s, r=%s(%p), d=%-3d, c=%d", name.c_str(), r->name(), r, delay, cost);

    if (autoRelease) {
      lms::release(r);
    }
  }
  
  void scheduleOnce() {
    void *s = nullptr;
    lms::Runnable *r = nullptr;

    SDL_LockMutex(mtx);
    {
      if (!runnables.empty()) {
        r = runnables.front();
        runnables.pop_front();
      }
    }
    SDL_UnlockMutex(mtx);
    
    if (r == nullptr) {
      return;
    }
    
    // 避免在lock范围内进行函数调用，否则可能会导致死锁
    launch(r, true);
  }
  
  void cancel() override {
    LMSLogDebug("Cancel runnables: q=%s, count=%d", name.c_str(), (int)runnables.size());
    
    SDL_LockMutex(mtx);
    {
      for (auto r : runnables) {
        LMSLogDebug("Cancel runnable : q=%s, t=host , r=%s(%p)", name.c_str(), r->name(), r);
        lms::release(r);
      }
      
      runnables = {};
    }
    SDL_UnlockMutex(mtx);
  }
  
private:
  std::string name;
  SDL_mutex *mtx;
  std::list<lms::Runnable *> runnables;
};

SDLApplication::SDLApplication(int argc, char **argv) {
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    LMSLogError("Can't initialize SDL: %s", SDL_GetError());
    exit(1);
  }

  this->argc = argc;
  this->argv = argv;
}

void SDLApplication::run(SDLAppDelegate *delegate) {
  _sdlMainThreadId = SDL_ThreadID();
  
  RunnableEvent = SDL_RegisterEvents(1);

  delegate->didFinishLaunchingApplication(argc, argv);

  SDL_Event event;
  for (;;) {
    SDL_WaitEvent(&event);
    if (event.type == SDL_QUIT) {
      break;
    }

    if (event.type == RunnableEvent) {
      auto queue = (SDLHostQueue *)event.user.data1;
      queue->scheduleOnce();
    }
  }
  
  delegate->willTerminateApplication();
}

namespace lms {

class SDLTimer : public Timer {
public:
  const char    *name;
  double         interval;
  lms::Runnable *runnable;
  SDL_Thread    *thread;
  std::atomic<bool> shouldQuit;

  SDLTimer(const char *name, double interval, lms::Runnable *r) {
    this->name       = strdup(name);
    this->shouldQuit = false;
    this->interval   = interval;
    this->runnable   = retain(r);
  }
  
  ~SDLTimer() {
    release(runnable);
    free((void *)name);
  }
};

static int runtimeTimerThread_SDL(SDLTimer *timer) {
  const double intervalMS = timer->interval * 1000;
  const Uint32 predelayMS = (Uint32)(intervalMS * 0.2);
  Uint32 begin  = SDL_GetTicks();
  LMSLogDebug("Timer start: name=%s, begin=%u, interval=%lf", timer->name, begin, intervalMS);
  
  bool shouldDebug = strcmp(timer->name, "DecodeTimer") == 0;
  
  while(!timer->shouldQuit) {
    timer->runnable->run();
    
    // 避免 runnable 执行过快（小于1ms)，导致下面的delay计算结果为0，从而进入短暂的忙循环
    SDL_Delay(predelayMS);

    Uint32 now = SDL_GetTicks();
    double diff = (double)(now - begin) / intervalMS;
    int delay = (int)((1.0 - diff + std::floor(diff)) * intervalMS);

    Uint32 expectedBreakPoint = SDL_GetTicks() + delay;
    do {
      delay = expectedBreakPoint - SDL_GetTicks();
      
      // 如果使用最小睡眠单位1ms不断重复，仍然会带来一定的CPU负载
      // 所以如果预期时间较长时，可以睡眠相对长的时间段，减轻这种负载
      if (delay > 10) {
        SDL_Delay(delay / 2);
      } else if (delay >= 5) {
        SDL_Delay(3);
      } else if (delay >= 1) {
        SDL_Delay(1);
      }
    } while(SDL_GetTicks() < expectedBreakPoint);
  }
  
  LMSLogDebug("Timer stop: name=%s", timer->name);
}

Timer *scheduleTimer(const char *name, double interval, std::function<void()> action) {
  if (name == nullptr) {
    name = "Undefined";
  }
    
  auto r = new LambdaRunnable(name, action);
  auto timer = new SDLTimer(name, interval, r);
  release(r);
  
  timer->thread = SDL_CreateThread((SDL_ThreadFunction)runtimeTimerThread_SDL, name, timer);
  
  return timer;
}

void invalidateTimer(Timer *t) {
  if (t == nullptr) {
    return;
  }
  
  // timer 实例一定是经过 scheduleTimer 方法创建的，所以可以安全地进行强制类型转换
  auto timer = static_cast<SDLTimer *>(t);

  timer->shouldQuit = true;
  
  SDL_WaitThread(timer->thread, nullptr);  
}

DispatchQueue *createDispatchQueue(const char *name, QueueType type) {
  if (type == QueueTypeHost) {
    return new SDLHostQueue(name);
  } else {
    return new SDLWorkerQueue(name);
  }
}

}
