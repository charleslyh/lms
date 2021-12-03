#include <lms/Player.h>
#include <lms/MediaSource.h>
#include <lms/Logger.h>
#include <extension/sdl/SDLApplication.h>
#include <extension/sdl/SDLView.h>
#include <extension/ffmpeg/FFVideoFile.h>

class PlayerAppDelegate: public SDLAppDelegate {
public:  
  void didFinishLaunchingApplication(int argc, char **argv) override {
    lms::init();
    lms::setLogLevel(lms::LogLevelVerbose);

    auto src = lms::autoRelease(new FFVideoFile(argv[1]));
    player = new lms::Player(src, lms::autoRelease(new SDLView));
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
  
  lms::dumpLeaks();
}
