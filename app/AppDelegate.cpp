#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Player.h"
#include "SDLApplication.h"


class PlayerAppDelegate: public SDLAppDelegate {
public:
  void didFinishLaunchingApplication(int argc, char **argv) override {
    lms::init();

    lms::VideoFile *src = lms::autoRelease(new lms::VideoFile("path/to/file"));
    player = new lms::Player(src);
    player->play();
  }

  void willTerminateApplication() override {
    player->stop();
    lms::release(player);

    lms::unInit();
  }

private:
  lms::Player *player;
};


int main(int argc, char *argv[]) {
  SDLApplication app(argc, argv);
  PlayerAppDelegate delegate;
  app.run(&delegate);
}

