#include "mix_naudio.h"
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
using namespace std;

//const char* filter_desc = "[in0][in1][in2]amix=inputs=3[out]";
// ./mix-audio ../audio/music.mp3 ../audio/vocals.mp3 ../audio/zpb.mp3
int main(int argc,const char** argv) {

	MixAudioFFMpeg* mix_audio = new MixAudioFFMpeg();

	int ret = mix_audio->Init();
	{
		unique_lock<mutex> loc(mix_audio->mux);
		mix_audio->SetAudioStream("../audio/music.mp3");
		mix_audio->SetAudioStream("../audio/vocals.mp3");
		mix_audio->SetAudioStream("../audio/sorry_shorter.mp3");
		mix_audio->GraphParseConfig();
		mix_audio->Start();
	}
	
	usleep(1000*1000);

	{	
		unique_lock<mutex> loc(mix_audio->mux);
		mix_audio->SetAudioStream("../audio/huanhu_shorter.mp3");
		mix_audio->SetAudioStream("../audio/sorry_longer.mp3");
		mix_audio->GraphParseConfig();
		mix_audio->Start();
	}
	// usleep(10000*1000);

	// {	
	// 	unique_lock<mutex> loc(mix_audio->mux);
	// 	mix_audio->SetAudioStream("../audio/huanhu.mp3");
	// 	mix_audio->GraphParseConfig();
	// 	mix_audio->Start();
	// }

	while(!mix_audio->MixSuccess()){
		usleep(500*1000);
	}
 	delete mix_audio;
	printf("end\n");
}
