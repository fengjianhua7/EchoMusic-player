#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "../outputs/ESP32_Touch_Console/minimp3.h"
#include <vector>
#include <fstream>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
using std::min;
struct Stream{std::vector<uint8_t> data;size_t at=0;};
struct Log{template<class T>void print(T){}void println(){}template<class T>void println(T){}int printf(const char*,...){return 0;}}Serial;
static bool saveMode=false,durationEstimated=false,trackSeeking=false;
static int volumePercent=100;
static uint32_t trackDurationMs=0,trackBaseMs=0;
static uint8_t packet[512],compressed[8192];
static mp3dec_t decoder;static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static std::vector<uint8_t> output;
static bool exact(Stream& s,uint8_t* out,size_t n){if(s.at+n>s.data.size())return false;memcpy(out,s.data.data()+s.at,n);s.at+=n;return true;}
static bool sendBlock(){output.insert(output.end(),packet,packet+512);return true;}
static void finishAudio(){}static void delay(int){}
static uint32_t be32(const uint8_t* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void inferDuration(const uint8_t* frame,unsigned size,const mp3dec_frame_info_t& info,int samples,int length){
 if(trackDurationMs)return;
 for(unsigned i=4;i+12<size&&i<192;i++){
  if(!memcmp(frame+i,"Xing",4)||!memcmp(frame+i,"Info",4)){
   uint32_t flags=be32(frame+i+4);if(flags&1){uint32_t frames=be32(frame+i+8);
    if(frames){trackDurationMs=(uint32_t)((uint64_t)frames*(unsigned)samples*1000/(unsigned)info.hz);durationEstimated=false;return;}}
  }
 }
 if(info.bitrate_kbps>0){trackDurationMs=(uint32_t)((uint64_t)(unsigned)length*8/(unsigned)info.bitrate_kbps);durationEstimated=true;}
}
static bool mp3(Stream& in, int length) {
  if(length<=0) {Serial.println("Content-Length required");return false;}
  mp3dec_init(&decoder);
  size_t used=0, out=0;
  uint32_t left=(uint32_t)length, phase=0, frames=0;
  int lastHz=0;
  uint64_t sourceSamples=0;
  bool inspected=false;
  while(left || used) {
    while(left && used<4096) {
      size_t n=min((size_t)left,min((size_t)1024,sizeof(compressed)-used));
      if(!exact(in,compressed+used,n)) return false;
      used+=n; left-=n;
    }
    if(!inspected && used>=4) {
      inspected=true;
      Serial.print("Audio first bytes: ");
      for(size_t i=0;i<used && i<16;i++)Serial.printf("%02X ",(unsigned)compressed[i]);
      Serial.println();
      if(!memcmp(compressed,"OggS",4)) {Serial.println("Received OGG, not MP3; decoder unsupported");return false;}
      if(!memcmp(compressed,"fLaC",4)) {Serial.println("Received FLAC, not MP3; decoder unsupported");return false;}
      if(!memcmp(compressed,"RIFF",4)) {Serial.println("Received WAV, not MP3");return false;}
    }
    mp3dec_frame_info_t info={};
    int samples=mp3dec_decode_frame(&decoder,compressed,(int)used,pcm,&info);
    if(info.frame_bytes<=0) {
      if(left && used<sizeof(compressed)) {
        size_t n=min((size_t)left,sizeof(compressed)-used);
        if(!exact(in,compressed+used,n)) return false;
        used+=n; left-=n; continue;
      }
      break;
    }
    if((size_t)info.frame_bytes>used) return false;
    if(samples>0&&!lastHz&&info.hz>0)inferDuration(compressed+info.frame_offset,(unsigned)(info.frame_bytes-info.frame_offset),info,samples,length);
    used-=info.frame_bytes;
    memmove(compressed,compressed+info.frame_bytes,used);
    if(samples>0) {
      if(info.hz<=0 || (info.channels!=1 && info.channels!=2)) return false;
      if(lastHz && lastHz!=info.hz) {Serial.println("Changing sample rate unsupported");return false;}
      if(!lastHz) Serial.printf("MP3: %d Hz, %d channels\n",info.hz,info.channels);
      lastHz=info.hz; frames++;
      if(trackBaseMs&&!saveMode&&(sourceSamples+(unsigned)samples)*1000/(unsigned)info.hz<=trackBaseMs){sourceSamples+=(unsigned)samples;continue;}
      for(int i=0;i<samples;i++) {
        bool skip=!saveMode&&sourceSamples*1000/(unsigned)info.hz<trackBaseMs;
        sourceSamples++;
        if(skip)continue;
        trackSeeking=false;
        int32_t value=pcm[i*info.channels];
        if(info.channels==2) value=(value+pcm[i*2+1])/2;
        int16_t v=(int16_t)(saveMode?value:((value*volumePercent)/100));
        // Basic rate conversion for bring-up; upgrade with low-pass filtering later.
        phase+=32000;
        while(phase>=(uint32_t)info.hz) {
          phase-=info.hz;
          packet[out++]=(uint8_t)v;
          packet[out++]=(uint8_t)((uint16_t)v>>8);
          if(out==512) {if(!sendBlock()) return false;out=0;}
        }
      }
    }
    delay(0);
  }
  if(out) {memset(packet+out,0,512-out);if(!sendBlock()) return false;}
  if(frames)finishAudio();
  Serial.printf("Decoded frames: %lu\n",(unsigned long)frames);
  return frames>0;
}

int main(){std::ifstream f("outputs/audio_test/test.mp3",std::ios::binary);assert(f);Stream s;s.data=std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});assert(mp3(s,(int)s.data.size()));auto whole=output;assert(whole.size()>32000*5*2);output.clear();s.at=0;trackBaseMs=5000;trackSeeking=true;assert(mp3(s,(int)s.data.size()));assert(!trackSeeking);size_t offset=32000*5*2;assert(whole.size()-offset==output.size());assert(std::equal(output.begin(),output.end(),whole.begin()+offset));
 mp3dec_frame_info_t info={};info.hz=44100;info.bitrate_kbps=128;uint8_t frame[64]={};memcpy(frame+20,"Xing",4);frame[27]=1;frame[31]=100;trackDurationMs=0;inferDuration(frame,64,info,1152,100000);assert(trackDurationMs==2612&&!durationEstimated);
 trackDurationMs=0;memset(frame,0,sizeof(frame));inferDuration(frame,64,info,1152,100000);assert(trackDurationMs==6250&&durationEstimated);
 puts("Actual MP3 decoder seek and Xing/CBR duration tests passed");return 0;}
