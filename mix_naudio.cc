#include "mix_naudio.h"
#include <iostream>
#include <thread>
#include <unistd.h>
using namespace std;
const char* GetFilterDesc(int number){
	string filter_desc;
	for(int i = 0;i<number;i++) {
		filter_desc =filter_desc +  "[in"+std::to_string(i)+"]";
	}
	filter_desc+="amix=inputs=";
	filter_desc=filter_desc + std::to_string(number)+"[out]";
	return filter_desc.c_str();
}
static void PrintErr(int err)
{
    char buf[1024] = { 0 };
    av_strerror(err, buf, sizeof(buf) - 1);
    cerr << buf << endl;
}
#define PrintError(err) 					\
						if(err < 0) 					\
						{											\
							PrintErr(err); 			\
							return err;					\
						}


MixAudioFFMpeg::MixAudioFFMpeg() : input_ctx1_(NULL),input_ctx2_(NULL),
																	codec_ctx1_(NULL),codec_ctx2_(NULL),
																	ctx1buff_(NULL),ctx2buff_(NULL),filter_frame_(NULL),
																	filter_graph_(NULL),filter_ctx1(NULL),
																	filter_ctx2(NULL),status(false),pkt_count_(0),reclSize_(0),pts_(0),num_stream_(0),stop(false),into_filter_(true){
	memset(input_ctx_,0,sizeof(input_ctx_));
	memset(filter_ctx_,0,sizeof(filter_ctx_));
}

MixAudioFFMpeg::~MixAudioFFMpeg() {
	this->UnInit();
}
int MixAudioFFMpeg::UnInit() {
  // AVFilterGraph* filter_graph_;
  // AVFilterContext* filter_ctx1;
  // AVFilterContext* filter_ctx2;
  // // AVFilterContext* filter_ctxout_;
	if(filter_ctx1) {
		avfilter_free(filter_ctx1);
		filter_ctx1 = NULL;
	}
	if(filter_ctx2) {
		avfilter_free(filter_ctx1);
		filter_ctx2 = NULL;
	}
	if(filter_ctxout_) {
		avfilter_free(filter_ctxout_);
		filter_ctxout_ = NULL;
	}
	if(filter_graph_) {
		avfilter_graph_free(&filter_graph_);
	}
	if( input_ctx1_ ) {
		avformat_close_input(&input_ctx1_);
	}
	if(input_ctx2_) {
		avformat_close_input(&input_ctx2_);
	}
	if(out_ctx_) {
		if(out_ctx_->pb) {
			avio_close(out_ctx_->pb);
		}
		avformat_free_context(out_ctx_);
	}
	if(codec_ctx1_) {
		avcodec_free_context(&codec_ctx1_);
	}
	if(codec_ctx2_) {
		avcodec_free_context(&codec_ctx2_);
	}
	if(encoder_) {
		avcodec_free_context(&encoder_);
	}
	if(ctx1buff_) {
		av_audio_fifo_free(ctx1buff_);
		ctx1buff_ = NULL;
	}
	if(ctx2buff_) {
		av_audio_fifo_free(ctx2buff_);
		ctx2buff_ = NULL;
	}
	if(filter_frame_) {
		av_frame_free(&filter_frame_);
	}
	
	//avfilter_inout_free(&filter_one_inout);
	//avfilter_inout_free(&filter_two_inout);
	//avfilter_inout_free(&filter_out_inout);

}
int MixAudioFFMpeg::Start() {
	if(status == false) {
		thread t = thread(&MixAudioFFMpeg::MixAuido,this);
		t.detach();
	}
	stop = false;
	return 0;
}
bool MixAudioFFMpeg::MixSuccess() {
	if(status == false && reclSize_ == total_stream_) {
		return true;
	}
	return false;
}
int MixAudioFFMpeg::Init() {

	int ret = 0;
	ret = OpenFileIo(OUT_PUT_FILE_NAME);
	if(ret  < 0) {
		return ret;
	}
	ret = this->InitFilter(encoder_);
	if(ret < 0) {
		return ret;
	}
	return 0;
}

int MixAudioFFMpeg::SetAudioStream(const char* file_name) {
	if(status) {
		stop = true;
	}
	if(num_stream_>=MAX_STREAM_NUM) {
		cout<<"Audio stream number is more max size !" <<endl;
		stop = false;
		return -1;
	}
	int ret = 0;
	ret = OpenInputCtx(&input_ctx_[num_stream_],&decode_ctx_[num_stream_],file_name);
	if (ret < 0) {
		stop = false;
		return ret;
	};
	ret = InitAudioFifo();
	if(ret < 0) {
		stop = false;
		return ret;
	}
	if(stop == true) {

		DemuxSream(num_stream_);
		++num_stream_;
		++total_stream_;
		if(num_stream_>=2 && into_filter_==false) {
			into_filter_ = true;
		}
		return 0;
	}
	if(num_stream_ == 0) {
		AddFilterToGraph(false);
	}else {
		AddFilterToGraph(true);
	}
	//av_dump_format(input_ctx_[num_stream_], stream_index,file_name, 0);
	DemuxSream(num_stream_);
	++num_stream_;
	if(num_stream_>=2 && into_filter_==false) {
		into_filter_ = true;
	}
	++total_stream_;
	return 0;
} 

int MixAudioFFMpeg::DemuxSream(int index) {
	thread tt = thread(&MixAudioFFMpeg::DemuxAudio,this,input_ctx_[index],
	 																		decode_ctx_[index],ctxbuff_[index],index);
	tt.detach();
}

int MixAudioFFMpeg::InitAudioFifo(){
	int ret;
	ctxbuff_[num_stream_] = av_audio_fifo_alloc(decode_ctx_[num_stream_]->sample_fmt,
																							input_ctx_[num_stream_]->streams[stream_index]->codecpar->channels,
																							30 * decode_ctx_[num_stream_]->frame_size);
	if(ctxbuff_[num_stream_]) {
		return 0;
	} else {
		return -1;
	}
}
int MixAudioFFMpeg::InitFilter(AVCodecContext* codec) {
	memset(filter_inout_,0,sizeof(filter_inout_));
	int ret = 0;
	filter_graph_ = avfilter_graph_alloc();
	const AVFilter* filter_out = avfilter_get_by_name("abuffersink");
	if (!filter_graph_) {
		av_log(NULL,AV_LOG_ERROR," avfilter_graph_alloc() is failed !.");
		return -1;
	}
	ret = avfilter_graph_create_filter(&filter_ctxout_, filter_out, "out", NULL, NULL,filter_graph_);
	if (ret < 0) {
		cout << " avfilter_graph_create_filter2 out failed\n";
		return -1;
	}
	ret = av_opt_set_bin(filter_ctxout_, "sample_rates",(uint8_t*)&codec->sample_rate, sizeof(codec->sample_rate)
		,AV_OPT_SEARCH_CHILDREN);
	if (ret < 0) {
		cout << " av_opt_set_bin sample_rates failed\n";
		return -1;
	}
	ret = av_opt_set_bin(filter_ctxout_, "channel_layouts", (uint8_t*)&codec->channel_layout, sizeof(codec->channel_layout)
		,AV_OPT_SEARCH_CHILDREN);
	if (ret < 0) {
		cout << " av_opt_set_bin  cahnnel_layouts failed\n";
		return -1;
	}
	ret = av_opt_set_bin(filter_ctxout_, "sample_fmts",  (uint8_t*)&codec->sample_fmt, sizeof(codec->sample_fmt)
		, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0) {
		cout << " av_opt_set_bin sample_fmts failed\n";
		return -1;
	}
}
int MixAudioFFMpeg::AddFilterToGraph(bool pre_stream_to_next) {

	int ret;
	string  mp3_stream = "in" + std::to_string(num_stream_);
	char args_stream[512];
	const AVFilter* filter = avfilter_get_by_name("abuffer");
	if(!filter) {
		av_log(NULL,AV_LOG_ERROR,"avfilter_get_by_name failed");
		return -1;
	}
	filter_inout_[num_stream_]  = avfilter_inout_alloc();
	AVFilterInOut* filter_inout = filter_inout_[num_stream_];
	
	snprintf(args_stream, sizeof(args_stream), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%I64d",
		decode_ctx_[num_stream_]->time_base.num,decode_ctx_[num_stream_]->time_base.den,
		input_ctx_[num_stream_]->streams[stream_index]->codecpar->sample_rate,
		av_get_sample_fmt_name(decode_ctx_[num_stream_]->sample_fmt),
		input_ctx_[num_stream_]->streams[stream_index]->codecpar->channel_layout
	);

	ret = avfilter_graph_create_filter(&filter_ctx_[num_stream_], filter, mp3_stream.c_str(), args_stream, NULL, filter_graph_);
	cout<<"=======hello==="<<endl;
	PrintError(ret);

	filter_inout->name = av_strdup(mp3_stream.c_str());
	filter_inout->filter_ctx = filter_ctx_[num_stream_];
	filter_inout->pad_idx = 0;
	if(pre_stream_to_next) {
		filter_inout_[num_stream_-1]->next = filter_inout;
	}
	filter_inout->next = NULL;
}

int MixAudioFFMpeg::AddFilterToGraph(bool pre_stream_to_next,int index) {
	int ret;
	string  mp3_stream = "in" + std::to_string(index);
	char args_stream[512];
	const AVFilter* filter = avfilter_get_by_name("abuffer");
	if(!filter) {
		av_log(NULL,AV_LOG_ERROR,"avfilter_get_by_name failed");
		return -1;
	}
	filter_inout_[index]  = avfilter_inout_alloc();
	AVFilterInOut* filter_inout = filter_inout_[index];
	
	snprintf(args_stream, sizeof(args_stream), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%I64d",
		decode_ctx_[index]->time_base.num,decode_ctx_[index]->time_base.den,
		input_ctx_[index]->streams[stream_index]->codecpar->sample_rate,
		av_get_sample_fmt_name(decode_ctx_[index]->sample_fmt),
		input_ctx_[index]->streams[stream_index]->codecpar->channel_layout
	);
	ret = avfilter_graph_create_filter(&filter_ctx_[index], filter, mp3_stream.c_str(), args_stream, NULL, filter_graph_);
	if(filter_ctx_[index]==nullptr) {
		printf("filter_ctx is nullptr\n");
		getchar();
	}
	PrintError(ret);

	filter_inout->name = av_strdup(mp3_stream.c_str());
	filter_inout->filter_ctx = filter_ctx_[index];
	filter_inout->pad_idx = 0;
	if(pre_stream_to_next) {
		filter_inout_[index-1]->next = filter_inout;
	}
	filter_inout->next = NULL;
}
int MixAudioFFMpeg::GraphParseConfig() {
	const char* filter_desc = GetFilterDesc(num_stream_);
	printf("filter_desc : %s\n",filter_desc);
	//getchar();
	if(stop) {
		if(filter_graph_) {
			avfilter_graph_free(&filter_graph_);
			filter_graph_ = nullptr;
		}
		int ret = this->InitFilter(encoder_);
		if(ret < 0) {
			return ret;
		}
		memset(filter_ctx_,0,sizeof(filter_ctx_));

		//filter_graph_->nb_filters = 0;
		for(int i = 0;i<num_stream_;i++) {
			if(i == 0) {
				AddFilterToGraph(false,i);
			}else {
				AddFilterToGraph(true,i);
			}
		}
	}
	int ret;
	AVFilterInOut* filter_out_inout = avfilter_inout_alloc();
	filter_out_inout->name = av_strdup("out");
	filter_out_inout->filter_ctx = filter_ctxout_;
	filter_out_inout->pad_idx = 0;
	filter_out_inout->next = NULL;

	AVFilterInOut* filter_outputs[num_stream_];
	for(int i = 0;i<num_stream_;i++) {
		filter_outputs[i] = filter_inout_[i];
		printf("filter_outputs[%d] : %x\n",i,filter_outputs[i]->name);
	}
	//printf("num_stream_ is %d\n",num_stream_);
	if(filter_graph_) {
		//printf("filter_desc : %s\n",filter_desc);
	}
	printf("hello woowowowooow=============\n");
	for(int i= 0;i<filter_graph_->nb_filters;i++){
		printf("filter_graph_->filter[%d] : %s\n",i,filter_graph_->filters[i]->name);
	}
	ret = avfilter_graph_parse_ptr(filter_graph_, filter_desc, &filter_out_inout, filter_outputs, NULL);
	if(ret<0){
		printf("aviflter_graph_parase %d\n",ret);
	}
	PrintError(ret);
	if (ret < 0) {
		cout << "avfilter_graph_parse_ptr failed\n";
		return -1;
	}
	ret = avfilter_graph_config(filter_graph_, NULL);
	if (ret < 0) {
		cout << "avfilter_graph_config failed\n";
		return -1;
	}
	const char* info = avfilter_graph_dump(filter_graph_, NULL);
	cout << info << endl;
	av_free((void*)info);
	return 0;
}
int MixAudioFFMpeg::OpenInputCtx(AVFormatContext** ctx,AVCodecContext **codec_ctx,const char* filename) {
	if(ctx!=nullptr||*ctx!=nullptr) {
		avformat_close_input(ctx);
	}
  int ret = 0;
	ret = avformat_open_input(ctx, filename, nullptr, nullptr);
	if (ret < 0) {
		av_log(NULL,AV_LOG_ERROR,"%s open_input is error",filename);
		return -1;
	}
	avformat_find_stream_info(*ctx, nullptr);
	if (ret < 0) {
		av_log(NULL,AV_LOG_ERROR,"%s avofmat_find_stream_info failed",filename);
		return -1;
	}
  stream_index = av_find_best_stream(*ctx,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);
  if(stream_index==AVERROR_STREAM_NOT_FOUND) {
    av_log(NULL,AV_LOG_ERROR,"%s av_find_best_stream(ctx,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0)",filename);
    return -1;
  }
	const AVCodec* codec = avcodec_find_decoder((*ctx)->streams[stream_index]->codecpar->codec_id);
	if (!codec) {
    av_log(NULL,AV_LOG_ERROR,"%s avcodec_find_decoder ctx failed",filename);
		return -1;
	}
	*codec_ctx = avcodec_alloc_context3(codec);
	avcodec_parameters_to_context(*codec_ctx, (*ctx)->streams[stream_index]->codecpar);
	ret = avcodec_open2(*codec_ctx, codec, nullptr);
	if (ret < 0) {
		av_log(NULL,AV_LOG_ERROR,"%s avcodec_open2 failed",filename);
		return -1;
	}
	return ret;
}

int MixAudioFFMpeg::AddAudioDataToFifo(AVAudioFifo* fifo,uint8_t **data,uint64_t data_len) {
	
}

int MixAudioFFMpeg::ReadFromFifo(AVAudioFifo* fifo,uint8_t**data,uint64_t data_len) {

}

int MixAudioFFMpeg::AddFilterAndToEncode(AVFilterContext* filter_ctx,AVFrame* frame) {
	int ret;
	{
		//printf("====%s======\n",filter_ctx->name);
		if (av_buffersrc_add_frame_flags(filter_ctx, frame, 0) < 0) {
			av_log(NULL, AV_LOG_ERROR, "Error while feeding the audio filtergraph1 \n");
			printf("num_stream : %d",num_stream_);
			return -1;
		}
	}
	//total heap usage: 14,231 allocs, 13,085 frees, 13,249,569 bytes allocated
	while (1) {
		InitFrameParamFormCtx(&filter_frame_, encoder_);
		av_frame_unref(filter_frame_);
		ret = av_buffersink_get_frame(filter_ctxout_, filter_frame_);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		{
			//cout<<"xxxxxxxxxxxxxxxxxxxxxxxxx" <<endl; 
			return 0;
		}
		else if (ret < 0) {

			av_log(NULL, AV_LOG_ERROR, "buffersink get frame fail, ret: %d\n",
				ret);
			return ret;
		}
		EncodeData(filter_frame_, 0);
	}
	return 0;
}
int MixAudioFFMpeg::InitFrame(AVFrame** frame) {
	/* Create a new frame to store the audio samples. */
	if (NULL == (*frame)) {
		if (!(*frame = av_frame_alloc())) {
			return AVERROR_EXIT;
		}
	}
	else {
		av_frame_unref(*frame);
	}

	return 0;
}
//==19704==   total heap usage: 13,657 allocs, 12,511 frees, 12,403,617 bytes allocated
int MixAudioFFMpeg::InitFrameParamFormCtx(AVFrame **frame,  AVCodecContext *codec_ctx) {
if (NULL == codec_ctx) {
		return -1;
	}

	int ret = InitFrame(frame);
	if (0 < ret) {
		return ret;
	}

	/* Set the frame's parameters, especially its size and format.
	 * av_frame_get_buffer needs this to allocate memory for the
	 * audio samples of the frame.
	 * Default channel layouts based on the number of channels
	 * are assumed for simplicity. */
	(*frame)->nb_samples = codec_ctx->frame_size;
	(*frame)->channel_layout = codec_ctx->channel_layout;
	(*frame)->format = codec_ctx->sample_fmt;
	(*frame)->sample_rate = codec_ctx->sample_rate;

	/* Allocate the samples of the created frame. This call will make
	 * sure that the audio frame can hold as many samples as specified. */
	if ((ret = av_frame_get_buffer((*frame), 0)) < 0) {
		char ptrChar[AV_ERROR_MAX_STRING_SIZE] = { 0 };
		av_strerror(ret, ptrChar, AV_ERROR_MAX_STRING_SIZE);
		av_frame_free(frame);
		return ret;
	}

	return 0;
}
int MixAudioFFMpeg::EncodeData(AVFrame* filt_frame, unsigned int stream_index) {
  int ret;
	filt_frame->pts = pts_;
	pts_ += 1000 * filt_frame->nb_samples / filt_frame->sample_rate;
	//cout<<"filter_frame pts : "<<filter_frame_->pts<<"\n";
	if(!into_filter_) {
		ret = avcodec_send_frame(encoder_, filt_frame);
	}else {
		ret = avcodec_send_frame(encoder_, filter_frame_);
	}
	if (ret < 0) {
		return ret;
	}
	while (ret >= 0) {
		AVPacket pkt;
		av_init_packet(&pkt);
		ret = avcodec_receive_packet(encoder_, &pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
			av_packet_unref(&pkt);
			break;
		}
		else if (ret < 0) {
			av_packet_unref(&pkt);
			return ret;
		}
		//cout<<"pkt pts : "<<pkt.pts<<"\n";
		/* rescale output packet timestamp values from codec to stream timebase */
		av_packet_rescale_ts(&pkt, encoder_->time_base,
			out_ctx_->streams[stream_index]->time_base);
		pkt.stream_index = stream_index;

		/* Write the compressed frame to the media file. */
		// log_packet(fmt_ctx, &pkt);
		
		//pkt.pts = pkt_count_ * (encoder_->time_base.num / encoder_->time_base.den);
		//pkt.pts = filter_frame_->pts;
		++pkt_count_;
		//pkt.dts = pkt.pts;
		//pkt.duration = encoder_->frame_size;
		//cout<<"pkt.pts "<<pkt.pts<<"\n";
		//cout<<"pkt.dts "<<pkt.dts<<"\n";
		//cout<<"encoder_->time_base : "<< encoder_->time_base.den<<endl;
		//<<"oencoder_->time_base : "<< encoder_->time_base.num<<endl;
		pkt.pts = av_rescale_q_rnd(
			pkt.pts, encoder_->time_base,
			out_ctx_->streams[stream_index]->time_base,
			(AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.dts = pkt.pts;
		pkt.duration = av_rescale_q_rnd(
			pkt.duration,encoder_->time_base,
			out_ctx_->streams[stream_index]->time_base,
			(AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

		ret = av_interleaved_write_frame(out_ctx_, &pkt);
		cout<<"encode a frame size1" <<endl; 
		// cout<<"write packet count "<<pkt_count_<<"\n";
		// ++pkt_count_;
		av_packet_unref(&pkt);
		if (ret < 0) {
			return -1;
		}
	}
	return ret;
}

void* MixAudioFFMpeg::DemuxAudio(AVFormatContext* ctx,AVCodecContext* codec,AVAudioFifo* fifo,int index) {
	int ret = 0;
	AVPacket packet;
	AVFrame* pFrame = av_frame_alloc();
	av_init_packet(&packet);
	while(status == false) {
		usleep(100*1000);
	}
	while (status) {
		if(stop) {
			usleep(100 * 1000);
			continue;
		}
		packet.data = NULL;
		packet.size = 0;
		//mux.lock();
		ret = av_read_frame(ctx, &packet);
		//mux.unlock();
		if (ret < 0)
		{
			av_packet_unref(&packet);
			PrintErr(ret);
			break;
		}
		//cout << packet.size << endl;
		ret = avcodec_send_packet(codec, &packet);
		if (ret < 0) {
			PrintErr(ret);
			break;
		}
		// while(1) {
		// 	ret = avcodec_receive_frame(*codec,pFrame);
		// 	if(ret < 0){
		// 		//av_frame_unref(pFrame);
		// 		break;
		// 	}
		// 	int size = av_audio_fifo_space(*fifo);
		// 	while (size < pFrame->nb_samples && status) {
		// 		size = av_audio_fifo_space(*fifo);
		// 	}
		// 	if (size >= pFrame->nb_samples) {
		// 		ret = av_audio_fifo_write(*fifo, (void**)pFrame->data, pFrame->nb_samples);
		// 		//cout << "write ctx3buff " << ret << " bytes\n";
		// 	}
		// 	av_frame_unref(pFrame);
		// }
		//av_packet_unref(&packet);
		ret = avcodec_receive_frame(codec, pFrame);
		int size = av_audio_fifo_space(fifo);
		while (size < pFrame->nb_samples && status && !stop) {
			size = av_audio_fifo_space(fifo);
		}
		if (size >= pFrame->nb_samples) {
			ret = av_audio_fifo_write(fifo, (void**)pFrame->data, pFrame->nb_samples);
			//cout << "write ctx3buff " << ret << " bytes\n";
		}
	}
	av_frame_free(&pFrame);
	//============短的音频结束后的处理=============
	unique_lock<mutex> loc(mux);
	if(num_stream_ == 1) {
		num_stream_--;
		reclSize_++;
		return nullptr;
	}
	stop = true;
	usleep(500*1000);
	index = GetInIndex(ctx);
	printf("index============ : %d\n",index);
	avformat_close_input(&input_ctx_[index]);
	avcodec_free_context(&decode_ctx_[index]);
	av_audio_fifo_free(ctxbuff_[index]);
	//avfilter_inout_free(&filter_inout_[index]);
	avfilter_free(filter_ctx_[index]);
	for(int i = index;i<num_stream_-1;i++) {
		input_ctx_[i] = input_ctx_[i+1];
		decode_ctx_[i] = decode_ctx_[i+1];
		encode_ctx_[i] = encode_ctx_[i+1];
		ctxbuff_[i] = ctxbuff_[i+1];
		filter_ctx_[i] = filter_ctx_[i+1];
		filter_inout_[i] = filter_inout_[i+1]; 
	}
	input_ctx_[num_stream_-1] = nullptr;
	decode_ctx_[num_stream_-1] = nullptr;
	encode_ctx_[num_stream_-1] = nullptr;
	ctxbuff_[num_stream_-1] = nullptr;
	filter_ctx_[num_stream_-1] = nullptr;
	filter_inout_[num_stream_-1] = nullptr;
	num_stream_--;
	//getchar();
	GraphParseConfig();
	//status = false;
	reclSize_++;
	if(num_stream_==1) {
		into_filter_ = false;
	}
	stop = false;
	return nullptr;
}
// ./mix-audio ../audio/music.mp3 ../audio/vocals.mp3 ../audio/zpb.mp3
//   0x00000000004e383d in av_read_frame (s=0x206a500, pkt=0x7ffff0e31ca0) at libavformat/demux.c:1450
// #7  0x00000000004d583f in MixAudioFFMpeg::DemuxAudio (this=0x203e010, ctx=0x203e038, codec=0x203e0a0, fifo=0x203e158, index=1)
int MixAudioFFMpeg::MixAuido() {
	int ret;
	status = true;

	while(status) {
		if(stop) {
			usleep(100 * 1000);
			continue;
		}
		if(reclSize_ >=total_stream_) {
			status = false;
		}
		//unique_lock<mutex> loc(mux);
		for(int i = 0;i < num_stream_;i++) {
			unique_lock<mutex> loc(mux);
			if(input_ctx_[i]==nullptr)break;
			AVFrame* frame = nullptr;
			if (av_audio_fifo_size(ctxbuff_[i]) > encoder_->frame_size) {
				
				ret = InitFrameParamFormCtx(&frame,decode_ctx_[i]);
				if(ret < 0) {
					cout<<"InitFraeParamFormCtx()"<<endl;
				}
				ret = av_audio_fifo_read(ctxbuff_[i], (void**)frame->data,encoder_->frame_size);
				if (ret <= 0) {
					av_frame_free(&frame);
					PrintErr(ret);
					printf("helowroorror\n");
					//status = false;
					//av_log(NULL,AV_LOG_ERROR,"av_audio_fifo_read faield %d\n",i);
					continue;
					//break;
				}
				if(into_filter_ == false) {
					printf("into filter is false\n");
					EncodeData(frame, 0);
				}else {
					ret = AddFilterAndToEncode(filter_ctx_[i],frame);
				}

				if(ret >= 0) {
					//cout<<"encode a frame size1" <<endl; 
				}
				av_frame_free(&frame);
			}
		}
		usleep(1);
	}
	av_write_trailer(out_ctx_);
}
int MixAudioFFMpeg::OpenFileIo(const char* filename) {
	int ret = 0;
	const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
	if (ret < 0) {
		cout << "avcodec_find_encoder out filed" << endl;
		return -1;
	}
	encoder_ = avcodec_alloc_context3(codec);
	if (!encoder_) {
		cout << "avcodec_alloc_context3 out failed" << endl;
		return -1;
	}
	encoder_->sample_rate = 44100;
	encoder_->bit_rate = 320000;
	encoder_->time_base.num = 1;
	encoder_->time_base.den = 44100;
	encoder_->channel_layout = AV_CH_LAYOUT_STEREO;
	encoder_->sample_fmt = (AVSampleFormat)AV_SAMPLE_FMT_FLTP;
	encoder_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	ret = avcodec_open2(encoder_, codec, nullptr);
	if (ret < 0) {
		cout << "aavcodec_open2 out failed" << endl;
		return -1;
	};
	ret = avformat_alloc_output_context2(&out_ctx_, nullptr, nullptr, filename);
	if (ret < 0) {
		cout << "avformat_alloc_output_context2 failed" << endl;
		return -1;
	};
	AVStream* audio_stream = avformat_new_stream(out_ctx_, nullptr);
	audio_stream->codecpar->codec_tag = 0;
	avcodec_parameters_from_context(audio_stream->codecpar, encoder_);
	ret = avio_open(&out_ctx_->pb, filename, AVIO_FLAG_WRITE);
	if (ret < 0) {
		cout << "avio_open(&out->pb, filename, AVIO_FLAG_WRITE); failed" << endl;
		return -1;
	};
	avformat_write_header(out_ctx_, nullptr);
	return ret;
}
void MixAudioFFMpeg::FlushBufferEncoder() {
	int ret;
	AVPacket pkt;
	ret = avcodec_send_frame(encoder_,NULL);
	if(ret < 0) {
		return;
	}
	for(;;) {
		av_init_packet(&pkt);
		ret = avcodec_receive_packet(encoder_,&pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
			av_packet_unref(&pkt);
			break;
		}
		else if (ret < 0) {
			av_packet_unref(&pkt);
			return;
		}
		//cout<<"pkt pts : "<<pkt.pts<<"\n";
		/* rescale output packet timestamp values from codec to stream timebase */
		av_packet_rescale_ts(&pkt, encoder_->time_base,
			out_ctx_->streams[stream_index]->time_base);
		pkt.stream_index = stream_index;

		/* Write the compressed frame to the media file. */
		// log_packet(fmt_ctx, &pkt);
		
		//pkt.pts = pkt_count_ * (encoder_->time_base.num / encoder_->time_base.den);
		//pkt.pts = filter_frame_->pts;
		++pkt_count_;
		//pkt.dts = pkt.pts;
		//pkt.duration = encoder_->frame_size;
		//cout<<"pkt.pts "<<pkt.pts<<"\n";
		//cout<<"pkt.dts "<<pkt.dts<<"\n";
		//cout<<"encoder_->time_base : "<< encoder_->time_base.den<<endl;
		//<<"oencoder_->time_base : "<< encoder_->time_base.num<<endl;
		pkt.pts = av_rescale_q_rnd(
			pkt.pts, encoder_->time_base,
			out_ctx_->streams[stream_index]->time_base,
			(AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.dts = pkt.pts;
		pkt.duration = av_rescale_q_rnd(
			pkt.duration,encoder_->time_base,
			out_ctx_->streams[stream_index]->time_base,
			(AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

		ret = av_interleaved_write_frame(out_ctx_, &pkt);
		// cout<<"write packet count "<<pkt_count_<<"\n";
		// ++pkt_count_;
		av_packet_unref(&pkt);
		if (ret < 0) {
			return;
		}
	}
}
