//
//  Core.h
//  lms
//
//  Created by yuhuachli on 2021/12/6.
//

#pragma once

#include <lms/Runtime.h>
#include <lms/Logger.h>

namespace lms {

typedef struct {
  const char *name;
  void (*setup)();
  void (*teardown)();
} Module;

} // lms
