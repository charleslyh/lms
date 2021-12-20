#pragma once

#include <lms/Foundation.h>

namespace lms {

class MediaSource;
class Stream;
class Coordinator;
class Cell;
class TimeSync;

class Player : virtual public Object {
public:
  Player(MediaSource *mediaSource, Cell *vrender);
  ~Player();
  
  void play();
  void stop();

private:
  MediaSource *mediaSource;

  Stream *vstream;
  Cell   *vrender;
  Coordinator *coordinator;

  Stream *astream;
  TimeSync *timesync;
};

}
