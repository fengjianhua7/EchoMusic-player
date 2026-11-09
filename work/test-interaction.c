#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define BG 0
#define FG 0
#define UI_GREEN 0x236C
static const char* keyRows[]={"1234567890","qwertyuiop","asdfghjkl","zxcvbnm"};
static const unsigned keyOffsets[]={0,0,12,36};
static void drawProgress(void){}
static unsigned source,page,total,count,hasNext,view,duration;static char keyText[64],result[128];static unsigned tx,ty,rects;static int eventPhase;
int Touch_Read(unsigned* x,unsigned* y,int* phase){*x=tx;*y=ty;*phase=eventPhase;return 1;}
void Player_Reply(const char* text){strcpy(result,text);}void drawList(void){view=0;}void drawNow(void){view=1;}void keyboard(void){view=2;}void keyField(void){}
void area(unsigned a,unsigned b,unsigned c,unsigned d,unsigned e){}void button(unsigned a,unsigned b,unsigned c,unsigned d,const char*e,int f){}void LCD_SetColors(unsigned a,unsigned b){}void utf8_text(unsigned a,unsigned b,const char*c){}
void ILI9341_DrawRectangle(unsigned x,unsigned y,unsigned w,unsigned h,int f){assert(x+w<=240&&y+h<=320);rects++;}
/* Immediate visual feedback; no command is issued until the finger is released. */
static unsigned pressId,pressView,pressSource,pressPage,feedbackActive,pressX,pressY,pressW,pressH;
static unsigned hitControl(unsigned x,unsigned y,unsigned* rx,unsigned* ry,unsigned* rw,unsigned* rh){
 unsigned i,row,col;
 const char** keys=keyRows;
 *rx=0;*ry=0;*rw=0;*rh=0;
 if(view==3){if(y<288)return 0;*rx=x<120?0:120;*ry=288;*rw=120;*rh=32;return x<120?80:81;}
 if(y<21){*rx=(x/80)*80;*ry=0;*rw=80;*rh=21;return x<80?1:x<160?3:2;}
 if(y>=22&&y<49){i=x/80;*rx=i*80;*ry=22;*rw=80;*rh=27;return 10+i;}
 if(y>=296){i=x/60;*rx=i*60;*ry=296;*rw=60;*rh=24;return 30+i;}
 if(y>=272){i=x/80;*rx=i*80;*ry=272;*rw=80;*rh=24;return 20+i;}
 if(view==4){if(y>=246){*rx=0;*ry=246;*rw=240;*rh=26;return 205;}return 0;}
 if(view==2){
  if(y>=50&&y<80&&x>=180){*rx=180;*ry=50;*rw=60;*rh=30;return 50;}
  if(y>=84&&y<208){row=(y-84)/31;if(x<keyOffsets[row])return 0;col=(x-keyOffsets[row])/24;if(row<4&&col<strlen(keys[row])){*rx=keyOffsets[row]+col*24;*ry=84+row*31;*rw=24;*rh=29;return 100+row*10+col;}return 0;}
  if(y>=210&&y<241){i=x/60;*rx=i*60;*ry=210;*rw=60;*rh=31;return 150+i;}
  if(y>=244){i=x/60;*rx=i*60;*ry=244;*rw=60;*rh=28;return 160+i;}return 0;
 }
 if(view==1){
  if(y>=202&&y<244&&duration){*rx=10;*ry=214;*rw=220;*rh=14;return 200;}
  if(y>=246){i=x/80;*rx=i*80;*ry=246;*rw=80;*rh=26;return 201+i;}return 0;
 }
 if(y>=246){i=x/80;if((i==0&&!page)||(i==2&&!hasNext))return 0;*rx=i*80;*ry=246;*rw=80;*rh=26;return 210+i;}
 if(y>=52&&y<244){i=(y-52)/24;if(i<count){*rx=4;*ry=52+i*24;*rw=232;*rh=23;return 220+i;}}
 return 0;
}
static void restorePress(void){
 uint16_t color=0xE71C;
 if(!feedbackActive)return;feedbackActive=0;
 if(view!=pressView||source!=pressSource||page!=pressPage)return;
 if(pressId==200){drawProgress();return;}
 if(pressId<=3)color=UI_GREEN;
 else if(pressId>=10&&pressId<=12)color=(pressId-10==source)?UI_GREEN:0xE71C;
 else if(pressId==21||pressId==163)color=UI_GREEN;
 else if(pressId>=220)color=((pressId-220)&1)?0xEF7C:BG;
 LCD_SetColors(color,BG);
 ILI9341_DrawRectangle(pressX+1,pressY+1,pressW-2,pressH-2,0);
 ILI9341_DrawRectangle(pressX+2,pressY+2,pressW-4,pressH-4,0);
}
static void showPress(void){LCD_SetColors(0xFC20,BG);ILI9341_DrawRectangle(pressX+1,pressY+1,pressW-2,pressH-2,0);ILI9341_DrawRectangle(pressX+2,pressY+2,pressW-4,pressH-4,0);feedbackActive=1;}
static void touch_poll(void){
 unsigned x,y,index,col,row,len,hx,hy,hw,hh,hit;int phase;char event[120];const char** keys=keyRows;
 if(!Touch_Read(&x,&y,&phase))return;
 hit=hitControl(x,y,&hx,&hy,&hw,&hh);
 if(phase==1){restorePress();pressView=view;pressSource=source;pressPage=page;pressId=hit;pressX=hx;pressY=hy;pressW=hw;pressH=hh;if(pressId)showPress();return;}
 if(phase==2){
  if(pressId==200&&pressView==view){unsigned pos=x<12?0:x>228?216:x-12;area(12,218,216,6,0xD69A);if(pos)area(12,218,pos,6,0xFC20);return;}
  if(hit!=pressId)restorePress();else if(pressId&&!feedbackActive)showPress();return;
 }
 restorePress();
 if(phase!=3||view!=pressView||source!=pressSource||page!=pressPage||!pressId)return;
 if(pressId==200)y=218;else if(hit!=pressId)return;
 if(view==3){if(y>=288)Player_Reply(x<120?"UI:LIST\n":"L\n");return;}
 if(y<21){if(x>=160)Player_Reply("L\n");else if(x>=80){if(view==2)Player_Reply("UI:LIST\n");else keyboard();}else Player_Reply("UI:REFRESH\n");return;}
 if(y>=22&&y<49){index=x/80;snprintf(event,sizeof(event),"UI:SOURCE:%u\n",index);Player_Reply(event);return;}
 if(y>=296){index=x/60;Player_Reply(index==0?"-\n":index==1?"UI:MODE\n":index==2?"+\n":"UI:STOP\n");return;}
 if(y>=272){index=x/80;Player_Reply(index==0?"UI:PREV\n":index==1?"P\n":"N\n");return;}
 if(view==4){if(y>=246)Player_Reply("UI:LIST\n");return;}
 if(view==2){
  if(y>=50&&y<80&&x>=180){drawList();return;}
  if(y>=84&&y<208){row=(y-84)/31;if(x<keyOffsets[row])return;col=(x-keyOffsets[row])/24;if(row<4&&col<strlen(keys[row])){len=(unsigned)strlen(keyText);if(len<sizeof(keyText)-1){keyText[len]=keys[row][col];keyText[len+1]=0;keyField();}}return;}
  if(y>=210&&y<241){snprintf(event,sizeof(event),"UI:PRESET:%u\n",x/60);drawList();Player_Reply(event);return;}
  if(y>=244){index=x/60;len=(unsigned)strlen(keyText);if(index==0){if(len)keyText[len-1]=0;keyField();}else if(index==1){if(len<sizeof(keyText)-1){keyText[len]=' ';keyText[len+1]=0;}keyField();}else if(index==2){keyText[0]=0;keyField();}else{snprintf(event,sizeof(event),"UI:SEARCH:%s\n",keyText);drawList();Player_Reply(event);}return;}
  return;
 }
 if(view==1){
  if(y>=202&&y<244&&duration){if(x<12)x=12;if(x>228)x=228;snprintf(event,sizeof(event),"UI:SEEK:%u\n",(x-12)*1000/216);Player_Reply(event);return;}
  if(y>=246)Player_Reply(x<80?"UI:LIST\n":x<160?"UI:MODE\n":"UI:DOWNLOAD\n");return;
 }
 if(y>=246){index=x/80;if(index==0&&page)Player_Reply("UI:PAGE:-1\n");else if(index==1)Player_Reply("UI:NOW\n");else if(index==2&&hasNext)Player_Reply("UI:PAGE:1\n");return;}
 if(y>=52&&y<244){index=(y-52)/24;if(index<count){snprintf(event,sizeof(event),"UI:PLAY:%u:%u\n",source,page*8+index);Player_Reply(event);}return;}
}static void event(unsigned x,unsigned y,int phase){tx=x;ty=y;eventPhase=phase;touch_poll();}
static void tap(unsigned x,unsigned y){unsigned before=rects;result[0]=0;event(x,y,1);assert(!result[0]);event(x,y,3);assert(rects>before);}
int main(void){view=0;source=2;page=2;count=8;hasNext=1;tap(80,230);assert(!strcmp(result,"UI:PLAY:2:23\n"));tap(200,30);assert(!strcmp(result,"UI:SOURCE:2\n"));
 tap(200,10);assert(!strcmp(result,"L\n"));tap(90,10);assert(view==2&&!result[0]);view=0;
 result[0]=0;event(20,30,1);event(100,30,2);event(100,30,3);assert(!result[0]);
 view=1;duration=100000;result[0]=0;event(30,218,1);event(200,260,2);event(200,260,3);assert(!strcmp(result,"UI:SEEK:870\n"));
 view=0;source=2;page=0;result[0]=0;event(60,60,1);page=1;event(60,60,3);assert(!result[0]);
 view=1;source=2;page=0;tap(200,30);assert(!strcmp(result,"UI:SOURCE:2\n"));
 view=2;tap(90,10);assert(!strcmp(result,"UI:LIST\n"));
 view=2;keyText[0]=0;tap(228,128);assert(!strcmp(keyText,"p"));tap(204,97);assert(!strcmp(keyText,"p9"));
 view=2;tap(200,255);assert(view==0&&!strcmp(result,"UI:SEARCH:p9\n"));
 view=1;tap(200,255);assert(!strcmp(result,"UI:DOWNLOAD\n"));
 view=4;result[0]=0;event(80,60,1);event(80,60,3);assert(!result[0]);tap(100,255);assert(!strcmp(result,"UI:LIST\n"));
 puts("Press feedback tests passed: no action on down, orange outline bounds, local reload, login/search hit areas, drag cancel, seek release, stale-page cancellation");}
