#include "SDLApplication.h"
#include <lms/Runtime-.h>
#include <lms/Logger.h>
extern "C" {
#include <SDL2/SDL.h>
}
#include <cmath>

static Uint32 RunnableEvent;

SDLApplication::SDLApplication(int argc, char **argv) {
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    LMSLogError("Can't initialize SDL: %s", SDL_GetError());
    exit(1);
  }

  this->argc = argc;
  this->argv = argv;
}

void SDLApplication::run(SDLAppDelegate *delegate) {
  RunnableEvent = SDL_RegisterEvents(1);

  delegate->didFinishLaunchingApplication(argc, argv);

  SDL_Event event;
  for (;;) {
    SDL_WaitEvent(&event);
    if (event.type == SDL_QUIT) {
      break;
    }

    if (event.type == RunnableEvent) {
      auto r = (lms::Runnable *)event.user.data1;
      
      r->run();

      lms::release(r);
    }

    lms::drainAutoReleasePool();
  }
  
  delegate->willTerminateApplication();
  
  // TODO: 任何已被调度的runnable，都应该在stop过程保证全部被调度完成后才可以终止
  // TODO: 下面的清理工作是不应该存在的
  while(SDL_PollEvent(&event) == 1) {
    if (event.type == RunnableEvent) {
      LMSLogVerbose("Cleanup runnable: %p", event.user.data1);
      auto r = (lms::Runnable *)event.user.data1;
      lms::release((lms::Runnable *)event.user.data1);
    }
  }  
}

namespace lms {

class SDLTimer : public Timer {
public:
  const char    *name;
  double         interval;
  lms::Runnable *runnable;
  SDL_Thread    *thread;
  bool           shouldQuit;

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
    name = "unkown";
  }
    
  auto r = new LambdaRunnable(action);
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

class SDLMainQueue : public DispatchQueue {
public:
  void async(lms::Runnable *runnable) override {
    SDL_Event event;
    
    SDL_zero(event);
    event.type = RunnableEvent;
    event.user.data1 = lms::retain(runnable);
    event.user.data2 = nullptr;
    event.user.code  = 0;
    
    SDL_PushEvent(&event);
  }
};

DispatchQueue *createDispatchQueue(const char *name) {
  return new SDLMainQueue();
}

}