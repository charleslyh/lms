#pragma once

#include "MediaSource.h"
#include <map>
#include <vector>

namespace lms {

class StreamMeta;
class Stream;
class PassiveMediaSource;
class Render;

Stream *createStream(MediaSource *source, std::vector<Render *> renders);

}
