#include <stdio.h>
#include <stdlib.h>
#include "../outputs/ESP32_Touch_Fix/lrc_core.h"
int main(void){static char text[32769];static LrcEntry lines[256];size_t n=fread(text,1,32768,stdin);unsigned count;text[n]=0;count=lrcParse(text,lines,256);printf("Public LRC parsed with firmware code: %u timed lines\n",count);return count?0:1;}
