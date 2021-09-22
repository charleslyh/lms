#include "SDLApplication.h"
#include "LMSFoundation/Logger.h"
#include <memory>
#include <algorithm>

static Uint32 customEventType;

SDL_Window *mainWindow;

SDLApplication::SDLApplication(int argc, char **argv) {
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    LMSLogError("Can't initialize SDL: %s", SDL_GetError());
    exit(1);
  }

  this->argc = argc;
  this->argv = argv;
}


void SDLApplication::run(SDLAppDelegate *delegate) {
  customEventType = SDL_RegisterEvents(1);

  // 创建一个SDL窗口用于在其中进行视频渲染
  mainWindow = SDL_CreateWindow("LMS Window",
                                SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED,
                                1920 / 2,
                                1080 / 2,
                                SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_GL_SetSwapInterval(1);

  delegate->didFinishLaunchingApplication(argc, argv);

  SDL_Event event;
  for (;;) {
    SDL_WaitEvent(&event);
    if (event.type == SDL_QUIT) {
      break;
    }

    if (event.type == customEventType) {
      auto r = (lms::Runnable *)event.user.data1;
      r->run();
      lms::release(r);
    }

    lms::drainAutoReleasePool();
  }

  delegate->willTerminateApplication();
}

static
void PostRunnable(lms::Runnable *runnable, bool skipRetain) {
  SDL_Event event;
  SDL_zero(event);
  
  if (!skipRetain) {
    lms::retain(runnable);
  }

  // event是被异步执行的，autoReleasePool有可能先于runnable被执行前就被清理，即：
  // 1. SDL_DispatchRunnable
  // 2. lms::drainAutoReleasePool
  // 3. runnable->run()
  // 因此，需要手动持有runnable的引用，才能保证资源在run之后被正确释放
  event.user.data1 = runnable;
  event.user.data2 = nullptr;
  event.user.code  = 0;
  event.type = customEventType;
  SDL_PushEvent(&event);
}

void SDL_DispatchRunnable(lms::Runnable *runnable) {
  PostRunnable(runnable, false);
}

Uint32 periodicTimerProc(Uint32 interval, lms::Runnable *runnable) {
  PostRunnable(runnable, false);
  return interval;
}

int SDL_ScheduleRunnable(int delayMS, lms::Runnable *runnable) {
  lms::retain(runnable);
  return SDL_AddTimer(delayMS, (SDL_TimerCallback)periodicTimerProc, runnable);
}
