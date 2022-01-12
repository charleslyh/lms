#pragma once

#include <lms/Foundation.h>

namespace lms {

class MediaSource;
class Stream;
class SourceDriver;
class Cell;
class TimeSync;

class Player : virtual public Object {
public:
  Player(MediaSource *mediaSource, Cell *vrender);
  ~Player();
  
  void play();
  void stop();
  
private:
  void doPlay();
  void doStop();

private:
  MediaSource *source;

  Stream *vstream;
  Cell   *vrender;
  SourceDriver *coordinator;

  Stream *astream;
  TimeSync *timesync;
};

}
