#include <assert.h>
#include <stdio.h>
#include "../outputs/ESP32_Touch_Fix/seek_core.h"
int main(void){Mp3SeekInfo m={0};Mp3SeekPlan p;unsigned i;uint8_t frame[180]={0};
 m.ready=1;m.totalBytes=641024;m.audioOffset=1024;m.audioBytes=640000;m.bitrate=128;m.durationMs=40000;p=seekPlan(&m,20000);assert(p.offset==289024&&p.decodeBaseMs==18000&&p.approximate&&p.offset%4==0);
 p=seekPlan(&m,1000);assert(!p.offset&&!p.decodeBaseMs);
 memset(&m,0,sizeof(m));memcpy(frame+20,"Xing",4);frame[27]=7;frame[30]=3;frame[31]=232;frame[33]=1;frame[34]=0x86;frame[35]=0xa0;for(i=0;i<100;i++)frame[36+i]=(uint8_t)(i*256/100);
 seekRecordFrame(&m,frame,sizeof(frame),128,100128,44100,1152,128);assert(m.ready&&m.vbr&&m.hasToc&&m.exactDuration&&m.durationMs==26122);p=seekPlan(&m,20000);assert(p.offset>60000&&p.offset<75000&&p.decodeBaseMs==18000);
 m.toc[40]=0;memset(&m,0,sizeof(m));frame[76]=0;seekRecordFrame(&m,frame,sizeof(frame),128,100128,44100,1152,128);assert(!m.hasToc);p=seekPlan(&m,20000);assert(p.offset<m.totalBytes&&p.offset%4==0);
 puts("Fast seek planner tests passed: CBR byte jump, 2s warm-up, Xing TOC, malformed TOC fallback and offset alignment");}
