#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Player.h"
#include "SDLApplication.h"
#ifdef _WIN32
  #include <direct.h>
  #define getcwd _getcwd // stupid MSFT "deprecation" warning
#else
  #include <unistd.h>
  #include <limits.h>
#endif

class PlayerAppDelegate: public SDLAppDelegate, public lms::DispatchQueue {
public:
  void didFinishLaunchingApplication(int argc, char **argv) override {
    char path[PATH_MAX] = {0};
    getcwd(path, PATH_MAX);
    printf("Current Working Directory: %s\n", path);

    lms::init({
      this
    });

    lms::VideoFile *src = lms::autoRelease(new lms::VideoFile(argv[1]));
    player = new lms::Player(src);
    player->play();
  }

  void willTerminateApplication() override {
    player->stop();
    lms::release(player);

    lms::unInit();
  }

public:
  void async(lms::Runnable *runnable) override {
    SDL_DispatchRunnable(runnable);
  }

private:
  lms::Player *player;
};


int main(int argc, char *argv[]) {
  SDLApplication app(argc, argv);
  PlayerAppDelegate delegate;
  app.run(&delegate);
}

