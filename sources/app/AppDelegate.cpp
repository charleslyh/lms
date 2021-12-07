#include <lms/LMS.h>
#include <extension/sdl/SDLApplication.h>
#include <extension/sdl/SDLView.h>
#include <extension/ffmpeg/FFVideoFile.h>

class PlayerAppDelegate: public SDLAppDelegate {
public:  
  void didFinishLaunchingApplication(int argc, char **argv) override {
    // 必须先初始化SDK才能使用
    lms::init();
    
    // 设置日志过滤等级，一般默认为Info，但在调试场景下，可以使用Verbose来获取更完备的信息
    lms::setLogLevel(lms::LogLevelInfo);

    auto src = new FFVideoFile(argv[1]);
    auto view = new SDLView;

    player = new lms::Player(src, view);
    player->play();

    lms::release(src);
    lms::release(view);
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
