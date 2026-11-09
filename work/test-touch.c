#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static unsigned view,source,page,total,count,hasNext;static unsigned duration;static char keyText[64],result[128];static unsigned tx,ty;
int Touch_Event(unsigned* x,unsigned* y){*x=tx;*y=ty;return 1;}
void Player_Reply(const char* p){strcpy(result,p);}
void drawList(void){view=0;}void keyField(void){}void keyboard(void){view=2;}void area(unsigned a,unsigned b,unsigned c,unsigned d,unsigned e){}void LCD_SetColors(unsigned a,unsigned b){}void utf8_text(unsigned a,unsigned b,const char*c){}
#define BG 0
#define FG 0
static void touch_poll(void){
 unsigned x,y,index,col,row,len;char event[120];const char* keys[]={"qwertyuiop","asdfghjkl","zxcvbnm","0123456789"};
 if(!Touch_Event(&x,&y))return;
 if(view==3){if(y>=288)Player_Reply(x<120?"UI:LIST\n":"L\n");return;}
 if(y<21){Player_Reply(x>=160?"L\n":"UI:REFRESH\n");return;}
 if(y>=22&&y<49){index=x/80;if(index==source&&(source==0||source==2)){keyboard();return;}if(index==source&&source==1){Player_Reply("UI:REFRESH\n");return;}snprintf(event,sizeof(event),"UI:SOURCE:%u\n",index);Player_Reply(event);return;}
 if(y>=296){index=x/60;Player_Reply(index==0?"-\n":index==1?"UI:MODE\n":index==2?"+\n":"UI:STOP\n");return;}
 if(y>=272){index=x/80;Player_Reply(index==0?"UI:PREV\n":index==1?"P\n":"N\n");return;}
 if(view==2){
  if(y>=50&&y<80&&x>=180){drawList();return;}
  if(y>=84&&y<208){row=(y-84)/31;col=x/24;if(row<4&&col<strlen(keys[row])){len=(unsigned)strlen(keyText);if(len<sizeof(keyText)-1){keyText[len]=keys[row][col];keyText[len+1]=0;keyField();}}return;}
  if(y>=210&&y<241){snprintf(event,sizeof(event),"UI:PRESET:%u\n",x/60);Player_Reply(event);return;}
  if(y>=244){index=x/80;len=(unsigned)strlen(keyText);if(index==0){if(len)keyText[len-1]=0;keyboard();}else if(index==1){keyText[0]=0;keyboard();}else{snprintf(event,sizeof(event),"UI:SEARCH:%s\n",keyText);Player_Reply(event);}return;}
  return;
 }
 if(view==1){
  if(y>=202&&y<244&&duration){if(x<12)x=12;if(x>228)x=228;snprintf(event,sizeof(event),"UI:SEEK:%u\n",(x-12)*1000/216);Player_Reply(event);return;}
  if(y>=246)Player_Reply(x<120?"UI:LIST\n":"UI:MODE\n");return;
 }
 if(y>=246){index=x/80;if(index==0&&page)Player_Reply("UI:PAGE:-1\n");else if(index==1)Player_Reply("UI:NOW\n");else if(index==2&&hasNext)Player_Reply("UI:PAGE:1\n");return;}
 if(y>=52&&y<244){index=(y-52)/24;if(index<count){snprintf(event,sizeof(event),"UI:PLAY:%u:%u\n",source,page*8+index);Player_Reply(event);}return;}
}
static void tap(unsigned x,unsigned y){tx=x;ty=y;result[0]=0;touch_poll();}
int main(void){view=0;source=2;page=2;count=8;hasNext=1;tap(80,230);assert(!strcmp(result,"UI:PLAY:2:23\n"));
 tap(200,255);assert(!strcmp(result,"UI:PAGE:1\n"));tap(10,280);assert(!strcmp(result,"UI:PREV\n"));tap(200,280);assert(!strcmp(result,"N\n"));tap(90,305);assert(!strcmp(result,"UI:MODE\n"));
 view=1;duration=100000;tap(228,218);assert(!strcmp(result,"UI:SEEK:1000\n"));tap(12,218);assert(!strcmp(result,"UI:SEEK:0\n"));
 duration=0;tap(100,218);assert(!result[0]);view=0;source=0;tap(170,30);assert(!strcmp(result,"UI:SOURCE:2\n"));
 view=0;source=2;tap(200,30);assert(view==2);view=3;tap(20,300);assert(!strcmp(result,"UI:LIST\n"));
 puts("Touch dispatch tests passed: paging, original row index, transport, seek bounds, QR return");return 0;}
