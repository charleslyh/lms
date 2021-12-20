#pragma once

#include <lms/MediaSource.h>
extern "C" {
#include <libavformat/avformat.h>
}

class FFVideoFile : public lms::MediaSource {
public:
  FFVideoFile(const char *path);
  ~FFVideoFile() override;

  int numberOfStreams() override;
  lms::StreamMeta getStreamMeta(size_t streamIndex) override;

  int open() override;
  void close() override;

  void loadPackets(int numberRequested) override;

private:
  char *path;
  AVFormatContext *context;
};
