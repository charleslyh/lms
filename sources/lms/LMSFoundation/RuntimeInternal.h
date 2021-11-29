//
//  RuntimeInternal.h
//  lms
//
//  Created by yuhuachli on 2021/11/29.
//

#pragma once
#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Runtime.h"

namespace lms {

class Timer : virtual public Object {
public:
  const char    *name;
  bool           shouldQuit;
  double         interval;
  lms::Runnable *runnable;
  void          *data; // for internal use, such as thread obj
  
  Timer(const char *name, double interval, lms::Runnable *r) {
    this->name       = strdup(name);
    this->shouldQuit = false;
    this->interval   = interval;
    this->runnable   = retain(r);
  }
  
  ~Timer() {
    release(runnable);
    free((void *)name);
  }
};

class LambdaRunnable : public Runnable {
public:
  LambdaRunnable(std::function<void()> l) : lambda(l) { }

  void run() override {
    lambda();
  }

private:
  std::function<void()> lambda;
};

}
