//
//  Runtime.cpp
//  lms
//
//  Created by yuhuachli on 2021/11/29.
//

#include "Runtime-.h"

namespace lms {

void dispatchAsync(DispatchQueue *queue, Runnable *runnable) {
  queue->async(runnable);
}

void dispatchAsync(DispatchQueue *queue, std::function<void()> action) {
  auto r = new LambdaRunnable(action);
  queue->async(r);
  lms::release(r);
}

}
