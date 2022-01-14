//
//  SDLAudioResampler.cpp
//  RuntimeSDL
//
//  Created by yuhuachli on 2022/1/14.
//

#include <lms/Foundation.h>
#include <lms/Cell.h>
extern "C" {
  #include <libavcodec/avcodec.h>
  #include "libavutil/avutil.h"
  #include <libavutil/opt.h>
  #include <libavformat/avformat.h>
  #include <libswscale/swscale.h>
  #include <libswresample/swresample.h>
  #include <libavutil/imgutils.h>
  #include <SDL2/SDL.h>
}

class SDLAudioResampler: public lms::Cell {
  AVStream *stream;
  SwrContext *context;
  int out_channel_layout;
  int out_sample_rate;
  AVSampleFormat out_sample_format;

public:
  SDLAudioResampler(AVStream *stream) {
    this->stream = stream;
    this->out_sample_format  = AV_SAMPLE_FMT_S16;
    this->out_sample_rate    = stream->codecpar->sample_rate;
    this->out_channel_layout = stream->codecpar->channel_layout;

    this->context = swr_alloc();
    int64_t in_channel_layout  = stream->codecpar->channel_layout;
    int in_nb_channels = stream->codecpar->channels;
    int in_nb_samples = 0;
       
    // get input audio channels
    bool channels_matches_layout = (in_nb_channels == av_get_channel_layout_nb_channels(in_channel_layout));
    if (!channels_matches_layout) {
      in_channel_layout = av_get_default_channel_layout(in_nb_channels);
    }
    
    int ret = 0;
    setOption("in_channel_layout",  in_channel_layout);
    setOption("in_sample_rate",     stream->codecpar->sample_rate);
    setOption("in_sample_fmt",      stream->codecpar->format);
    setOption("out_channel_layout", out_channel_layout);
    setOption("out_sample_rate",    out_sample_rate);
    setOption("out_sample_fmt",     out_sample_format);

    ret = swr_init(context);
  }

  ~SDLAudioResampler() {
    swr_free(&this->context);
  }
  
  inline void setOption(const char *name, int value) {
    av_opt_set_int(context, name, value, 0);
  }

public:
  void start() override {}
  void stop() override {}
  
  void didReceivePipelineMessage(const lms::PipelineMessage& msg) override {
    auto avfrm = (AVFrame *)msg.at("frame").value.ptr;
    int out_linesize = 0;
    uint8_t **resampled_data = NULL;
    int resampled_data_size = 0;
    int out_nb_channels = av_get_channel_layout_nb_channels(out_channel_layout);

    int max_out_nb_samples = av_rescale_rnd(avfrm->nb_samples,
                                            out_sample_rate,
                                            avfrm->sample_rate,
                                            AV_ROUND_UP);
    
    int ret = av_samples_alloc_array_and_samples(&resampled_data,
                                                 &out_linesize,
                                                 out_nb_channels,
                                                 max_out_nb_samples,
                                                 out_sample_format,
                                                 0);
    
    if (!resampled_data) {
      return;
    }
    
    int64_t progressive_delay = swr_get_delay(context, avfrm->sample_rate) + avfrm->nb_samples;
    int out_nb_samples = av_rescale_rnd(progressive_delay, out_sample_rate, avfrm->sample_rate, AV_ROUND_UP);
    
    if (out_nb_samples > max_out_nb_samples) {
      av_free(resampled_data[0]);
      
      av_samples_alloc(resampled_data,
                       &out_linesize,
                       out_nb_channels,
                       out_nb_samples,
                       out_sample_format,
                       1);
    }

    out_nb_samples = swr_convert(context, resampled_data, out_nb_samples, (const uint8_t **)avfrm->data, avfrm->nb_samples);

    resampled_data_size = av_samples_get_buffer_size(&out_linesize,
                                                     out_nb_channels,
                                                     out_nb_samples,
                                                     out_sample_format,
                                                     1);
    
    AVFrame *frame_resampled = av_frame_alloc();
    frame_resampled->data[0] = (uint8_t *)av_malloc(resampled_data_size);
    memcpy(frame_resampled->data[0], resampled_data[0], resampled_data_size);
    frame_resampled->linesize[0] = resampled_data_size;
    frame_resampled->opaque = frame_resampled->data[0];
    frame_resampled->display_picture_number = avfrm->display_picture_number;
    frame_resampled->pts = avfrm->pts;
    frame_resampled->nb_samples = out_nb_samples;
    frame_resampled->format = out_sample_format;
    frame_resampled->sample_rate = out_sample_rate;

    lms::PipelineMessage frmMsg;
    frmMsg["type"]  = "media_frame";
    frmMsg["frame"] = frame_resampled;
    deliverPipelineMessage(frmMsg);

    av_freep(&resampled_data[0]);
    av_freep(&resampled_data);
  }
};


namespace lms {

Cell *createAudioResampler(AVStream *stream) {
  return new SDLAudioResampler(stream);
}

}
