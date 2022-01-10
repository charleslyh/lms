#pragma once

#include <lms/MediaSource.h>
extern "C" {
#include <libavformat/avformat.h>
}

class FFMediaFile : public lms::MediaSource {
public:
  FFMediaFile(const char *path);
  ~FFMediaFile() override;

  int numberOfStreams() override;
  lms::StreamMeta getStreamMeta(size_t streamIndex) override;

  int open() override;
  void close() override;

  void loadPackets(int numberRequested) override;

private:
  char *path;
  AVFormatContext *context;
  void *obsLP;
};
