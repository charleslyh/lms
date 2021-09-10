#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Player.h"
#include "App/SDLApplication.h"

class PlayerAppDelegate: public SDLAppDelegate {
public:
  void didFinishLaunchingApplication(int argc, char **argv) override {
    lms::VideoFile *src = lms::autoRelease(new lms::VideoFile("path/to/file"));
    player = new lms::Player(src);
    player->play();
  }

  void willTerminateApplication() override {
    player->stop();
    lms::release(player);
  }

private:
  lms::Player *player;
};

int main(int argc, char *argv[]) {
  PlayerAppDelegate delegate;
  SDLApplication app(argc, argv);
  app.run(&delegate);
}

