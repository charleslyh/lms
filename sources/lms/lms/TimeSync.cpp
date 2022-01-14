//
//  TimeSync.cpp
//  lms
//
//  Created by yuhuachli on 2022/1/14.
//

#include "TimeSync.h"
#include "Logger.h"
#include <memory.h>
extern "C" {
#include <SDL2/SDL.h>
}

namespace lms {

TimeSync::TimeSync() {
  timePivot  = 0;
  tickPivot = 0;
}

double TimeSync::getPlayingTime() const {
  uint32_t ticksPassed = SDL_GetTicks() - tickPivot;
  double secondsPassed = (double)ticksPassed / 1000.0;
  return timePivot + secondsPassed;
}

void TimeSync::updateTimePivot(double time) {
  LMSLogVerbose("time=%lf", time);
  timePivot  = time;
  tickPivot = SDL_GetTicks();
}

}
