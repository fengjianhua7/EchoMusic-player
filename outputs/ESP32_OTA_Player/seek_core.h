#pragma once
#include <stdint.h>
#include <string.h>
typedef struct {
 uint32_t totalBytes,audioOffset,audioBytes,durationMs,bitrate;
 uint8_t ready,vbr,hasToc,exactDuration,toc[100];
} Mp3SeekInfo;
typedef struct {uint32_t offset,decodeBaseMs;uint8_t approximate;} Mp3SeekPlan;
static uint32_t seekBe32(const uint8_t* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void seekRecordFrame(Mp3SeekInfo* m,const uint8_t* frame,unsigned size,uint32_t offset,uint32_t total,unsigned hz,unsigned samples,unsigned bitrate){
 unsigned i;
 if(m->ready){if(bitrate&&bitrate!=m->bitrate)m->vbr=1;return;}
 m->ready=1;m->totalBytes=total;m->audioOffset=offset;m->audioBytes=total>offset?total-offset:0;m->bitrate=bitrate;
 for(i=4;i+12<size&&i<192;i++){
  if(!memcmp(frame+i,"Xing",4)||!memcmp(frame+i,"Info",4)){
   uint32_t flags=seekBe32(frame+i+4);unsigned at=i+8;m->vbr=!memcmp(frame+i,"Xing",4);
   if(flags&1){if(at+4>size)break;{uint32_t frames=seekBe32(frame+at);if(frames&&hz){m->durationMs=(uint32_t)((uint64_t)frames*samples*1000/hz);m->exactDuration=1;}}at+=4;}
   if(flags&2){if(at+4>size)break;{uint32_t bytes=seekBe32(frame+at);if(bytes&&bytes<=m->audioBytes)m->audioBytes=bytes;}at+=4;}
   if((flags&4)&&at+100<=size){memcpy(m->toc,frame+at,100);m->hasToc=1;for(at=1;at<100;at++)if(m->toc[at]<m->toc[at-1])m->hasToc=0;}
   break;
  }
 }
 if(!m->durationMs&&bitrate)m->durationMs=(uint32_t)((uint64_t)m->audioBytes*8/bitrate);
}
static Mp3SeekPlan seekPlan(const Mp3SeekInfo* m,uint32_t targetMs){
 Mp3SeekPlan p={0,0,0};uint32_t base,relative;uint64_t bytes;
 if(!m->ready||targetMs<2500||m->totalBytes<8192)return p;
 base=targetMs-2000; // Warm up frame synchronization and the Layer III bit reservoir.
 if(m->hasToc&&m->durationMs){
  uint64_t percent=(uint64_t)base*10000/m->durationMs;unsigned index=(unsigned)(percent/100),fraction=(unsigned)(percent%100);
  unsigned a,b;if(index>99)index=99;a=m->toc[index];b=index<99?m->toc[index+1]:256;
  bytes=(uint64_t)m->audioBytes*(a*100+(b-a)*fraction)/25600;relative=(uint32_t)bytes;
 }else if(!m->vbr&&m->bitrate){relative=(uint32_t)((uint64_t)base*m->bitrate/8);}
 else if(m->durationMs){relative=(uint32_t)((uint64_t)m->audioBytes*base/m->durationMs);}
 else return p;
 bytes=(uint64_t)m->audioOffset+relative;
 if(bytes>m->totalBytes-4096)bytes=m->totalBytes-4096;
 p.offset=(uint32_t)bytes&~3UL;
 if(p.offset<=m->audioOffset){p.offset=0;return p;}
 p.decodeBaseMs=(!m->vbr&&m->bitrate)?(uint32_t)((uint64_t)(p.offset-m->audioOffset)*8/m->bitrate):base;
 p.approximate=1;return p;
}
