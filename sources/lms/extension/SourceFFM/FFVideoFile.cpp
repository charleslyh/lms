#include "FFVideoFile.h"
#include <lms/MediaSource.h>
#include <lms/Logger.h>
#include <lms/Runtime.h>


FFVideoFile::FFVideoFile(const char *path) {
  LMSLogVerbose("Path=%s", path);

  this->context = nullptr;
  this->path = strdup(path);
}

FFVideoFile::~FFVideoFile() {
  assert(context == nullptr);

  free(this->path);
}

int FFVideoFile::numberOfStreams() {
  return context->nb_streams;
}

lms::StreamMeta FFVideoFile::getStreamMeta(size_t streamIndex) {
  lms::StreamMeta meta;
  
  if (streamIndex < context->nb_streams) {
    AVStream *stream = context->streams[streamIndex];
    meta["source_type"]   = "avformat";
    meta["media_type"]    = (uint64_t)stream->codecpar->codec_type;
    meta["stream_class"]  = "AVStream";
    meta["stream_object"] = stream;
  }
  
  return meta;
}

int FFVideoFile::open() {
  LMSLogDebug("source=%p", this);
  
  int rt = 0;

  rt = avformat_open_input(&context, path, nullptr, nullptr);
  if (rt != 0) {
    LMSLogError("Failed opening video file: %s", path);
    return rt;
  }

  rt = avformat_find_stream_info(context, nullptr);
  if (rt != 0) {
    LMSLogError("Failed finding stream info");
    return rt;
  }
  
  av_dump_format(context, 0, path, 0);
  return 0;
}

void FFVideoFile::close() {
  LMSLogDebug("source=%p", this);

  lms::mainQueue()->cancel(this);

  avformat_close_input(&context);
}

void FFVideoFile::loadPackets(int numberRequested) {
  LMSLogVerbose("numberRequested=%d", numberRequested);
    
  // TODO: 使用独立的queue来加载数据
  async(lms::mainQueue(), this, [this, numberRequested] {
    for (int i = 0; i< numberRequested; i += 1) {
      AVPacket *avpkt = av_packet_alloc();
      int rt = av_read_frame(context, avpkt);
      if (rt >= 0) {
        LMSLogVerbose("Loaded: st=%d, flags=0x%-2x, dts=%" PRIu64
                      ", pts=%" PRIu64 ", dur=%" PRIu64 ", sz=%-6d",
                      avpkt->stream_index,
                      avpkt->flags,
                      avpkt->dts,
                      avpkt->pts,
                      avpkt->duration,
                      avpkt->size);
        
        lms::PipelineMessage msg;
        msg["type"]          = "media_packet";
        msg["stream_object"] = context->streams[avpkt->stream_index];
        msg["packet_object"] = avpkt;
        this->deliverPacketMessage(msg);

        av_packet_unref((AVPacket *)avpkt);
      } else {
        av_packet_free(&avpkt);
      }
    }
  });
}
