//
//  TimeSync.hpp
//  lms
//
//  Created by yuhuachli on 2022/1/14.
//

#pragma once
#include "Foundation.h"
#include "Logger.h"
#include <memory.h>
#include <inttypes.h>
extern "C" {
#include <SDL2/SDL.h>
}

namespace lms {

class TimeSync : virtual public Object {
public:
  TimeSync() {
    timePivot  = 0;
    tickPivot = 0;
  }
  
  double getPlayingTime() const {
    uint32_t ticksPassed = SDL_GetTicks() - tickPivot;
    double secondsPassed = (double)ticksPassed / 1000.0;
    return timePivot + secondsPassed;
  }
  
  void updateTimePivot(double time) {
    LMSLogVerbose("time=%lf", time);
    timePivot  = time;
    tickPivot = SDL_GetTicks();
  }
  
private:
  std::atomic<double>   timePivot;
  std::atomic<uint64_t> tickPivot;
};

}
