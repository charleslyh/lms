#pragma once

#include <lms/Foundation.h>

namespace lms {

class PassiveMediaSource;
class Stream;
class Coordinator;
class Render;
class TimeSync;

class Player : virtual public Object {
public:
  Player(PassiveMediaSource *src, Render *videoRender);
  ~Player();
  
  void play();
  void stop();

private:
  PassiveMediaSource *source;

  Stream *vstream;
  Render *vrender;
  Coordinator *coordinator;

  Stream *astream;
  TimeSync *timesync;
};

}
