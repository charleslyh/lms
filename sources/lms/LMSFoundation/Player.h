#pragma once

#include "LMSFoundation/Foundation.h"

namespace lms {

class PassiveMediaSource;
class VideoStream;
class Render;

class Player : virtual public Object {
public:
  Player(PassiveMediaSource *src, Render *videoRender);
  ~Player();
  
  void play();
  void stop();

private:
  PassiveMediaSource *source;
  Render      *vrender;
  VideoStream *vstream;
};

}
