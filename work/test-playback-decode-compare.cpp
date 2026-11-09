#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "../outputs/ESP32_Playback_Fix/minimp3.h"
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
#include "../outputs/ESP32_Playback_Fix/seek_core.h"
static Mp3SeekInfo seekInfo;
static uint32_t seekReadOffset,seekDecodeBaseMs,frameAbsoluteOffset,seekStartedAt;
static std::string playbackError;
static uint32_t millis(){return 0;}
static uint8_t packet[512],compressed[8192];
static mp3dec_t decoder;static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static std::vector<uint8_t> output;
static bool exact(Stream& s,uint8_t* out,size_t n){if(s.at+n>s.data.size())return false;memcpy(out,s.data.data()+s.at,n);s.at+=n;return true;}
static bool sendBlock(){output.insert(output.end(),packet,packet+512);return true;}
static void finishAudio(){}static void delay(int){}
static uint32_t be32(const uint8_t* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void inferDuration(const uint8_t* frame,unsigned size,const mp3dec_frame_info_t& info,int samples,int length){
 if(seekReadOffset)return;
 seekRecordFrame(&seekInfo,frame,size,frameAbsoluteOffset,(uint32_t)length,(unsigned)info.hz,(unsigned)samples,(unsigned)info.bitrate_kbps);
 if(!trackDurationMs||seekInfo.exactDuration){trackDurationMs=seekInfo.durationMs;durationEstimated=!seekInfo.exactDuration;}
}
static bool mp3(Stream& in, int length) {
  if(length<=0) {Serial.println("Content-Length required");return false;}
  mp3dec_init(&decoder);
  size_t used=0, out=0;
  uint32_t left=(uint32_t)length, phase=0, frames=0;
  int lastHz=0;
  uint64_t sourceSamples=0,discardUntilSamples=0;
  bool inspected=false;
  while(left || used) {
    if(trackSeeking&&millis()-seekStartedAt>15000){Serial.println("Seek timeout (15 seconds)");playbackError="定位超时，请重试或重新播放";return false;}
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
    frameAbsoluteOffset=seekReadOffset+(uint32_t)length-left-(uint32_t)used+(uint32_t)info.frame_offset;
    if(samples>0&&info.hz>0)inferDuration(compressed+info.frame_offset,(unsigned)(info.frame_bytes-info.frame_offset),info,samples,length);
    used-=info.frame_bytes;
    memmove(compressed,compressed+info.frame_bytes,used);
    if(samples>0) {
      if(info.hz<=0 || (info.channels!=1 && info.channels!=2)) return false;
      if(lastHz && lastHz!=info.hz) {Serial.println("Changing sample rate unsupported");return false;}
      if(!lastHz){
        Serial.printf("MP3: %d Hz, %d channels\n",info.hz,info.channels);
        if(!saveMode&&trackBaseMs>seekDecodeBaseMs)discardUntilSamples=((uint64_t)(trackBaseMs-seekDecodeBaseMs)*(unsigned)info.hz+999)/1000;
      }
      lastHz=info.hz; frames++;
      int firstSample=0;
      if(sourceSamples<discardUntilSamples){uint64_t remaining=discardUntilSamples-sourceSamples;firstSample=remaining>=(unsigned)samples?samples:(int)remaining;}
      sourceSamples+=(unsigned)samples;
      if(firstSample==samples)continue;
      trackSeeking=false;
      for(int i=firstSample;i<samples;i++) {
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
int main(){std::ifstream f("outputs/audio_test/test.mp3",std::ios::binary);assert(f);Stream s;s.data=std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});auto data=s.data;assert(mp3(s,(int)s.data.size()));auto whole=output;unsigned long long hash=14695981039346656037ULL;for(auto byte:whole){hash^=byte;hash*=1099511628211ULL;}printf("Normal PCM: bytes=%u fingerprint=%016llx\n",(unsigned)whole.size(),hash);auto info=seekInfo;assert(info.ready&&info.bitrate);
 output.clear();s.at=0;trackBaseMs=1000;trackSeeking=true;assert(mp3(s,(int)s.data.size()));size_t offset=32000*1*2;assert(whole.size()-offset==output.size());assert(std::equal(output.begin(),output.end(),whole.begin()+offset));
 auto p=seekPlan(&info,8000);assert(p.offset>0&&p.offset<data.size());s.data.assign(data.begin()+p.offset,data.end());s.at=0;seekReadOffset=p.offset;seekDecodeBaseMs=p.decodeBaseMs;trackBaseMs=8000;trackSeeking=true;output.clear();assert(mp3(s,(int)s.data.size()));assert(!trackSeeking);assert(s.at<data.size()*6/10);long expected=(long)whole.size()-32000*8*2;long error=std::abs((long)output.size()-expected);assert(error<=4*1152*2);
 printf("Actual fast seek decoded only %u/%u bytes; timeline difference <= %ld ms; exact short seek retained\n",(unsigned)s.at,(unsigned)data.size(),error/64);}
