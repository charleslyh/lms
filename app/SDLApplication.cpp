#include <list>
#include <memory>
#include <algorithm>
#include "SDLApplication.h"


SDLApplication::SDLApplication(int argc, char **argv) {
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    fprintf(stderr, "Can't initialize SDL: %s\n", SDL_GetError());
    exit(1);
  }

  this->argc = argc;
  this->argv = argv;
}


void SDLApplication::run(SDLAppDelegate *delegate) {
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
    
    auto cpy = blocks;
    std::for_each(cpy.begin(), cpy.end(), [] (const std::tuple<BlockPF, void *, void *>& item) {
      auto& act = std::get<0>(item);
      act(std::get<1>(item), std::get<2>(item));
    });

    lms::drainAutoReleasePool();
  }

  delegate->willTerminateApplication();
}