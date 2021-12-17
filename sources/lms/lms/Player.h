#pragma once

#include <lms/Foundation.h>

namespace lms {

class PassiveMediaSource;
class Stream;
class Coordinator;
class Cell;
class TimeSync;

class Player : virtual public Object {
public:
  Player(PassiveMediaSource *src, Cell *render);
  ~Player();
  
  void play();
  void stop();

private:
  PassiveMediaSource *source;

  Stream *vstream;
  Cell   *vrender;
  Coordinator *coordinator;

  Stream *astream;
  TimeSync *timesync;
};

}
