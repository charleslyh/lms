#include "FFMediaFile.h"
#include <lms/MediaSource.h>
#include <lms/Logger.h>
#include <lms/Runtime.h>
#include <lms/Events.h>


FFMediaFile::FFMediaFile(const char *path) {
  LMSLogVerbose("Path=%s", path);

  this->context = nullptr;
  this->path = strdup(path);
}

FFMediaFile::~FFMediaFile() {
  assert(context == nullptr);

  free(this->path);
}

int FFMediaFile::numberOfStreams() {
  return context->nb_streams;
}

lms::StreamMeta FFMediaFile::getStreamMeta(size_t streamIndex) {
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

int FFMediaFile::open() {
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
  
  q = lms::createDispatchQueue("LMS_FFMediaFile", lms::QueueTypeWorker);
  
  obsLP = lms::addEventObserver("load_packets", nullptr, [this] (const char *nm, void *sender, const lms::EventParams& p) {
    uint32_t count = lms::variantsGetInt(p, "count");
    loadPackets(count);
  });
  
  return 0;
}

void FFMediaFile::close() {
  LMSLogDebug("source=%p", this);
  
  lms::removeEventObserver(obsLP);

  lms::release(q);
  q = nullptr;

  avformat_close_input(&context);
}

void FFMediaFile::loadPackets(int count) {
  LMSLogVerbose("loadPackets: count=%d", count);
    
  // TODO: 使用独立的queue来加载数据
  async(q, [this, count] {
    AVPacket pkt;
    
    for (int i = 0; i< count; i += 1) {
      int rt = av_read_frame(context, &pkt);
      
      if (rt >= 0) {
        LMSLogVerbose("Loaded: st=%d, flags=0x%-2x, dts=%" PRIu64
                      ", pts=%" PRIu64 ", dur=%" PRIu64 ", sz=%-6d",
                      pkt.stream_index,
                      pkt.flags,
                      pkt.dts,
                      pkt.pts,
                      pkt.duration,
                      pkt.size);
        
        lms::PipelineMessage msg;
        msg["type"]          = "media_packet";
        msg["stream_object"] = context->streams[pkt.stream_index];
        msg["packet_object"] = &pkt;
        this->deliverPacketMessage(msg);
      }

      av_packet_unref(&pkt);
    }
  });
}
