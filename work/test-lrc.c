#include <assert.h>
#include <stdio.h>
#include "../outputs/ESP32_Touch_Console/lrc_core.h"
int main(void){LrcEntry e[8];unsigned n;
 n=lrcParse("[offset:-500]\n[00:03.125]three\n[00:01.2][00:02.34]two tags\n[00:00.1]start\n",e,8);
 assert(n==4);assert(e[0].ms==0);assert(e[1].ms==700);assert(e[2].ms==1840);assert(e[3].ms==2625);
 assert(lrcAt(e,n,699)==0);assert(lrcAt(e,n,700)==1);assert(lrcAt(e,n,9999)==3);
 n=lrcParse("[ar:artist]\n[00:60]invalid\n[00:00.001]valid\n[00:02]second\n",e,8);assert(n==2);assert(e[0].ms==1);assert(lrcAt(e,n,0)==-1);
 n=lrcParse("\xEF\xBB\xBF[01:02.003]bom\r\n",e,8);assert(n==1&&e[0].ms==62003&&!strcmp(e[0].text,"bom"));
 n=lrcParse("[00:01]a\n[00:02]b\n[00:03]c\n",e,2);assert(n==2);
 n=lrcParse("[00:01.1234]fraction\n[offset:1000]\n",e,8);assert(n==1&&e[0].ms==2123);
 puts("LRC parsing/timing tests passed");return 0;}
