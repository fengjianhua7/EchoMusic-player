#include <assert.h>
#include <stdio.h>
#include "../outputs/ESP32_Interaction_Fix/audio_gain.h"
int main(void){AudioGain g;int16_t old,x;int i;audioGainInit(&g,25);assert(audioGainApply(&g,32767,25)==8191);old=8191;
for(i=0;i<1024;i++){x=audioGainApply(&g,32767,30);assert(x>=old&&x-old<=3);old=x;}assert(g.gain==g.target);
for(i=0;i<1024;i++){x=audioGainApply(&g,32767,0);assert(x<=old&&old-x<=11);old=x;}assert(x==0);
audioGainInit(&g,100);assert(audioGainApply(&g,-32768,100)==-32768&&audioGainApply(&g,32767,100)==32767);puts("Gain ramp bounds, monotonic transition, zero and full-scale endpoints passed");}
