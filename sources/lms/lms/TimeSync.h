//
//  TimeSync.hpp
//  lms
//
//  Created by yuhuachli on 2022/1/14.
//

#pragma once
#include "Foundation.h"
#include <inttypes.h>

namespace lms {

class TimeSync : virtual public Object {
public:
  TimeSync();
  double getPlayingTime() const;
  void updateTimePivot(double time);
  
private:
  std::atomic<double>   timePivot;
  std::atomic<uint64_t> tickPivot;
};

}
