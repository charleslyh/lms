#include "plugins/Runtime+SDL.h"
#include "LMSFoundation/Runtime.h"
#include "LMSFoundation/RuntimeInternal.h"
#include "LMSFoundation/Logger.h"
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
  
  // 清除那些已被分派，但未被执行的runnable任务。否则会造成资源泄漏
  while(SDL_PollEvent(&event) == 1) {
    if (event.type == RunnableEvent) {
      lms::release((lms::Runnable *)event.user.data1);
    }
  }

  delegate->willTerminateApplication();
}


namespace lms {


static int runtimeTimerThread_SDL(Timer *timer) {
  const double intervalMS = timer->interval * 1000;
  const Uint32 preDelayMS = (Uint32)(intervalMS * 0.2);
  Uint32 begin  = SDL_GetTicks();
  LMSLogDebug("Timer start: name=%s, begin=%u, interval=%lf", timer->name, begin, intervalMS);
  
  while(!timer->shouldQuit) {
    LMSLogVerbose("Run timer on: ticks=%u", SDL_GetTicks());
    timer->runnable->run();
    
    // 避免 runnable 执行过快（小于1ms)，导致下面的delay计算结果为0，从而进入短暂的忙循环
    SDL_Delay(preDelayMS);

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

Timer *scheduleTimer(const char *name, double interval, std::function<void()> lambda) {
  if (name == nullptr) {
    name = "unkown";
  }
    
  auto r = new LambdaRunnable(lambda);
  Timer *timer = new Timer(name, interval, r);
  release(r);
  
  timer->data = SDL_CreateThread((SDL_ThreadFunction)runtimeTimerThread_SDL, name, timer);
  
  return timer;
}

void invalidateTimer(Timer *timer) {
  if (timer == nullptr) {
    return;
  }

  auto thread = (SDL_Thread *)timer->data;
  
  timer->shouldQuit = true;
  
  SDL_WaitThread(thread, nullptr);
  
  release(timer);
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
