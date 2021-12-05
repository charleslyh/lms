//
//  Runtime.cpp
//  lms
//
//  Created by yuhuachli on 2021/11/29.
//

#include "Runtime.h"

namespace lms {

void dispatch(DispatchQueue *queue, void *sender, Runnable *runnable) {
  queue->enqueue(sender, runnable);
}

void dispatch(DispatchQueue *queue, void *sender, std::function<void()> action) {
  auto r = new LambdaRunnable(action);
  queue->enqueue(sender, r);
  lms::release(r);
}

}
