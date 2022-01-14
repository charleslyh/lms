//
//  Stream.hpp
//  LoggerConsole
//
//  Created by yuhuachli on 2022/1/14.
//

#pragma once
#include "Foundation.h"
#include "Cell.h"

namespace lms {

class Stream : public Cell {
public:
  Stream(const StreamMeta &meta, Cell *decoder, Cell *resampler, Cell *renderDriver) {
    this->meta         = meta;
    this->streamObject = meta.at("stream_object").value.ptr;
    this->renderDriver = lms::retain(renderDriver);
    this->resampler    = lms::retain(resampler);
    this->decoder      = lms::retain(decoder);
  }

  ~Stream() {
    lms::release(resampler);
    lms::release(decoder);
    lms::release(renderDriver);
  }
  
  void start() override {
    if (resampler) {
      decoder->addReceiver(resampler);
      resampler->addReceiver(renderDriver);
    } else {
      decoder->addReceiver(renderDriver);
    }

    renderDriver->start();
    decoder->start();
  }
  
  void stop() override {
    decoder->stop();
    renderDriver->stop();
    
    if (resampler) {
      decoder->removeReceiver(resampler);
      resampler->removeReceiver(renderDriver);
    } else {
      decoder->removeReceiver(renderDriver);
    }
  }
  
  void didReceivePipelineMessage(const PipelineMessage& msg) override {
    if (msg.at("stream_object").value.ptr == streamObject) {
      decoder->didReceivePipelineMessage(msg);
    }
  }
  
private:
  StreamMeta meta;
  void *streamObject;
  Cell *decoder;
  Cell *resampler;
  Cell *renderDriver;
};

}
