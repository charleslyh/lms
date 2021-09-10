#pragma once

#include <list>
#include <memory>
#include <SDL2/SDL.h>
#include "LMSFoundation/Foundation.h"

class SDLAppDelegate {
public:
  virtual void didFinishLaunchingApplication(int argc, char **argv) = 0;
  virtual void willTerminateApplication() = 0;
};

typedef int (*BlockPF)(void *context, void *user_data);

class SDLApplication {
public:
  SDLApplication(int argc, char **argv);
  void run(SDLAppDelegate *delegate);

private:
  int  argc;
  char **argv;

  SDL_Window *win;

  std::list<std::tuple<BlockPF, void *, void *>> blocks;
};
