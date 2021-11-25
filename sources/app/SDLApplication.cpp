#include "SDLApplication.h"
#include "LMSFoundation/Logger.h"
#include <memory>
#include <algorithm>

extern "C" {
#include <SDL2/SDL.h>
}

static Uint32 RunnableEvent;

SDLApplication::SDLApplication(int argc, char **argv) {
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
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

static
void PostRunnable(lms::Runnable *runnable) {
  SDL_Event event;
  SDL_zero(event);
  
  event.type = RunnableEvent;
  event.user.data1 = lms::retain(runnable);
  event.user.data2 = nullptr;
  event.user.code  = 0;
  SDL_PushEvent(&event);
}

void SDL_DispatchRunnable(lms::Runnable *runnable) {
  PostRunnable(runnable);
}

Uint32 periodicTimerProc(Uint32 interval, lms::Runnable *runnable) {
  PostRunnable(runnable);
  return interval;
}

int SDL_ScheduleRunnable(int delayMS, lms::Runnable *runnable) {
  lms::retain(runnable);
  return SDL_AddTimer(delayMS, (SDL_TimerCallback)periodicTimerProc, runnable);
}
