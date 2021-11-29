//
//  Runtime.hpp
//  lms
//
//  Created by yuhuachli on 2021/11/29.
//

#pragma once
#include "LMSFoundation/Foundation.h"
#include <algorithm>

namespace lms {

class Runnable : virtual public Object {
public:
  virtual void run() = 0;
};

class DispatchQueue : virtual public Object {
public:
  virtual void async(Runnable *runnable) = 0;
};

DispatchQueue *createDispatchQueue(const char *queueName);

void dispatchAsync(DispatchQueue *queue, Runnable *runnable);
void dispatchAsync(DispatchQueue *queue, std::function<void()> lambda);

class Timer;

// Creates a timer and schedules it on a new thread.
Timer *scheduleTimer(const char *name, double interval, std::function<void()> lambda);

// Invalidates the timer scheduled by scheduleTimer, and release it after invalidated.
void invalidateTimer(Timer *timer);

}
