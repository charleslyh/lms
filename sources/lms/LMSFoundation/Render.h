#pragma once

#include "LMSFoundation/Foundation.h"
#include "LMSFoundation/Decoder.h"
#include <map>
#include <string>

namespace lms {

class Render : public FrameAcceptor {
public:
  virtual void prepare(std::map<std::string, void*> codecMeta) = 0;
  virtual void teardown() = 0;
};

}
