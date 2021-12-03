//
//  Runtime-.h
//  lms
//
//  Created by yuhuachli on 2021/11/29.
//

#pragma once
#include <lms/Foundation.h>
#include <lms/Runtime.h>

namespace lms {

class LambdaRunnable : public Runnable {
public:
  LambdaRunnable(std::function<void()> a) : act(a) { }
  
  void run() override {
    act();
  }
  
private:
  std::function<void()> act;
};

}
