//
//  SDLSpeaker.cpp
//  RuntimeSDL
//
//  Created by yuhuachli on 2022/1/14.
//

#include "SDLSpeaker.h"

namespace lms {

Cell *createSpeaker(AVStream* stream, TimeSync *tsync) {
  return new SDLSpeaker(stream, tsync);
}

}
