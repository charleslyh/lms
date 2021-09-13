#include <memory>
#include <algorithm>
#include "SDLApplication.h"

static Uint32 customEventType;

SDLApplication::SDLApplication(int argc, char **argv) {
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Can't initialize SDL: %s\n", SDL_GetError());
    exit(1);
  }

  this->argc = argc;
  this->argv = argv;
}


void SDLApplication::run(SDLAppDelegate *delegate) {
  customEventType = SDL_RegisterEvents(1);

  // 创建一个SDL窗口用于在其中进行视频渲染
  SDL_CreateWindow("LMS Window",
                   SDL_WINDOWPOS_CENTERED,
                   SDL_WINDOWPOS_CENTERED,
                   1920 / 2,
                   1080 / 2,
                   SDL_WINDOW_METAL | SDL_WINDOW_ALLOW_HIGHDPI);

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

void SDL_DispatchRunnable(lms::Runnable *runnable) {
  SDL_Event event;
  SDL_zero(event);

  // event是被异步执行的，autoReleasePool有可能先于runnable被执行前就被清理，即：
  // 1. SDL_DispatchRunnable
  // 2. lms::drainAutoReleasePool
  // 3. runnable->run()
  // 因此，需要手动持有runnable的引用，才能保证资源在run之后被正确释放
  event.user.data1 = lms::retain(runnable);
  event.user.data2 = nullptr;
  event.user.code  = 0;
  event.type = customEventType;
  SDL_PushEvent(&event);
}