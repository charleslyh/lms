//
//  LMS.h
//  lms
//
//  Created by yuhuachli on 2021/12/6.
//

#pragma once

#include <lms/Foundation.h>
#include <lms/Logger.h>
#include <lms/MediaSource.h>
#include <lms/Player.h>
#include <lms/Render.h>

namespace lms {

typedef struct {
} InitParams;

void init(InitParams params = {});
void unInit();

} // namespace lms
