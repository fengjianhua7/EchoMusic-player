#ifndef PLAYER_AUDIO_GAIN_H
#define PLAYER_AUDIO_GAIN_H
#include <stdint.h>
typedef struct {int32_t gain,target,step;} AudioGain;
static void audioGainInit(AudioGain* g,unsigned percent){g->gain=g->target=(int32_t)(percent*32768U/100U);g->step=1;}
static int16_t audioGainApply(AudioGain* g,int32_t sample,unsigned percent){
 int32_t target=(int32_t)(percent*32768U/100U),distance;
 if(target!=g->target){g->target=target;distance=target-g->gain;if(distance<0)distance=-distance;g->step=(distance+1023)/1024;if(!g->step)g->step=1;}
 if(g->gain<g->target){g->gain+=g->step;if(g->gain>g->target)g->gain=g->target;}
 else if(g->gain>g->target){g->gain-=g->step;if(g->gain<g->target)g->gain=g->target;}
 return (int16_t)(sample*g->gain/32768);
}
#endif
