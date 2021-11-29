//
//  Runtime.cpp
//  lms
//
//  Created by yuhuachli on 2021/11/29.
//

#include "LMSFoundation/RuntimeInternal.h"

namespace lms {

void dispatchAsync(DispatchQueue *queue, Runnable *runnable) {
  queue->async(runnable);
}

void dispatchAsync(DispatchQueue *queue, std::function<void()> lambda) {
  auto r = new LambdaRunnable(lambda);
  queue->async(r);
  lms::release(r);
}

}
