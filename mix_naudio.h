#ifndef __MIX_AUDIO_FFMPEG_H__
extern "C" {
#include <libavcodec/avcodec.h>
#include<libavfilter/avfilter.h>
#include<libavfilter/buffersink.h>
#include<libavfilter/buffersink.h>
#include<libavformat/avformat.h>
#include<libavutil/avutil.h>
#include<libavdevice/avdevice.h>
#include<libavutil/fifo.h>
#include<libavutil/audio_fifo.h>
#include<libavutil/samplefmt.h>
#include <libavfilter/buffersrc.h> 
}
#include <stdint.h>
#include <mutex>
#define MAX_STREAM_NUM 10
#define OUT_PUT_FILE_NAME "out2.mp3"


const char* GetFilterDesc(int num_stream);

class MixAudioFFMpeg{
public:
  MixAudioFFMpeg();
  ~MixAudioFFMpeg();
  bool MixSuccess();
  int Start();
  int SetAudioStream(const char* file_name);
  int DemuxSream(int index);

  int Init();
  int UnInit();
  //int InitFilter(const char* filter_desc,int* sample_rate,uint64_t* channel_layout,uint8_t* sample_fmt);

  int InitFilter(AVCodecContext* codec);
  int AddFilterToGraph(bool pre_stream_to_next = true);
  int AddFilterToGraph(bool pre_stream_to_next,int index);
  int GraphParseConfig();


  int InitAudioFifo();
  int OpenInputCtx(AVFormatContext** ctx,AVCodecContext **codec_ctx,const char* filename);

  int OpenFileIo(const char* filename);
  /**
   * @brief将解码后的数据放入对应的fifo中
   * @param 
  */
  int AddAudioDataToFifo(AVAudioFifo* fifo,uint8_t **data,uint64_t data_len);

  int ReadFromFifo(AVAudioFifo* fifo,uint8_t**data,uint64_t data_len);


  int AddFilterAndToEncode(AVFilterContext* filter_ctx,AVFrame* frame);
  int InitFrameParamFormCtx(AVFrame **frame, AVCodecContext *codec_ctx);
  int InitFrame(AVFrame** frame);
  int EncodeData(AVFrame* filt_frame, unsigned int stream_index);
  int MixAuido();
  void* DemuxAudio(AVFormatContext* ctx,AVCodecContext* codec,AVAudioFifo* fifo,int index) ;
  int GetInIndex(AVFormatContext* ctx) {
    for(int i = 0;i<num_stream_;i++) {
      if(ctx == input_ctx_[i]) {
        return i;
      }
    }
    return -1;
  }

 // void FlushBufferDecoder();
  void FlushBufferEncoder();
  bool stop;
  uint64_t num_stream_;
  uint64_t total_stream_;
public:
  bool into_filter_;
  AVFormatContext* input_ctx_[MAX_STREAM_NUM];
  AVFormatContext* input_ctx1_;
  AVFormatContext* input_ctx2_;
  AVFormatContext* out_ctx_;

  AVCodecContext* decode_ctx_[MAX_STREAM_NUM];

  AVCodecContext* encode_ctx_[MAX_STREAM_NUM];

  AVCodecContext*  codec_ctx1_;
  AVCodecContext*  codec_ctx2_;
  AVCodecContext*  encoder_;

  AVAudioFifo* ctxbuff_[MAX_STREAM_NUM];
  AVAudioFifo* ctx1buff_;
  AVAudioFifo* ctx2buff_;



  AVFilterContext* filter_ctx_[MAX_STREAM_NUM];
  AVFilterInOut* filter_inout_[MAX_STREAM_NUM];
  AVFilterGraph* filter_graph_;
  AVFilterContext* filter_ctx1;
  AVFilterContext* filter_ctx2;
  AVFilterContext* filter_ctxout_;




  AVFrame* filter_frame_;
  bool status;
  int stream_index;
  int pkt_count_;
  int reclSize_;
  std::mutex mux;
  std::mutex mux2;
  uint64_t pts_; 
};


#endif
