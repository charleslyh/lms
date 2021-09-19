#pragma once

#include "LMSFoundation/Foundation.h"

namespace lms {

class PassivePacketSource;
class Decoder;
class Render;
class FramesBuffer;

class Player : virtual public Object {
public:
  Player(PassivePacketSource *src);
  ~Player();
  
  void play();
  void stop();
  void setVideoRender(Render *videoRender);

private:
  PassivePacketSource *src;
  Decoder      *videoDecoder;
  FramesBuffer *videoFramesBuffer;
  Render  *videoRender;
};

}
