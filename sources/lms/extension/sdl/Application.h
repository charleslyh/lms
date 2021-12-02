#pragma once

#include <lms/Runtime.h>

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
