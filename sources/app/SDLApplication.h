#pragma once

#include "LMSFoundation/Foundation.h"
#include <list>
#include <memory>

class SDLAppDelegate {
public:
  virtual void didFinishLaunchingApplication(int argc, char **argv) = 0;
  virtual void willTerminateApplication() = 0;
};


class SDLApplication {
public:
  SDLApplication(int argc, char **argv);
  void run(SDLAppDelegate *delegate);

private:
  int  argc;
  char **argv;
};


void SDL_DispatchRunnable(lms::Runnable *runnable);
int SDL_ScheduleRunnable(int delayMS, lms::Runnable *runnalbe);

typedef struct SDL_Window SDL_Window;
extern SDL_Window *mainWindow;
