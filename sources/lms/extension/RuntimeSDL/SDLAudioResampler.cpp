//
//  SDLAudioResampler.cpp
//  RuntimeSDL
//
//  Created by yuhuachli on 2022/1/14.
//

#include "SDLAudioResampler.h"

namespace lms {

Cell *createAudioResampler(AVStream *stream) {
  return new SDLAudioResampler(stream);
}

}
