#include "stm32f10x.h"
#include "lcd/bsp_ili9341_lcd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
extern const uint16_t ui_hanzi_codes[];
extern const unsigned ui_hanzi_count;
extern const uint8_t ui_hanzi_bits[][32];
extern uint32_t Audio_PlayedSamples(void);
volatile uint32_t ui_ms;
int Storage_Command(const char* line);
void Storage_Poll(void);
void Touch_Init(void);
void Touch_Calibrate(void);
int Touch_Event(unsigned* x,unsigned* y);
int Touch_Read(unsigned* x,unsigned* y,int* phase);
int Touch_IsCalibrated(void);
int Touch_IsPersisted(void);
static int hexval(char c);
static void keys_poll(void);
static void touch_poll(void);
static void utf8_text(unsigned x,unsigned y,const char* text);
#define BG 0xF7BE
#define FG 0x18E3
#define UI_GREEN 0x236C
#define GRAY 0x7BEF
static unsigned source=2,page,total,count,hasNext,view;
static unsigned playing,paused,volume=25,mode,estimated,seeking;
static uint32_t elapsed,duration;
static char titles[8][128],nowTitle[128],lyrics[128],keyText[64];
static unsigned textWidth(const char* text){unsigned n=0;const unsigned char* p=(const unsigned char*)text;while(*p){if(*p<128){n+=8;p++;}else if((*p&0xf0)==0xe0&&p[1]&&p[2]){n+=16;p+=3;}else if((*p&0xe0)==0xc0&&p[1]){n+=16;p+=2;}else p++;}return n;}
static void area(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color){LCD_SetColors(FG,color);ILI9341_Clear(x,y,w,h);}
static void button(unsigned x,unsigned y,unsigned w,unsigned h,const char* label,int active){unsigned tw=textWidth(label);area(x+1,y+1,w-2,h-2,active?UI_GREEN:0xE71C);LCD_SetColors(active?WHITE:FG,active?UI_GREEN:0xE71C);utf8_text(x+(tw<w?(w-tw)/2:2),y+(h-16)/2,label);}
static void wrapText(unsigned x,unsigned y,const char* text,unsigned rows){
 char buf[128];unsigned used=0,width=0,n;const unsigned char* p=(const unsigned char*)text;
 while(*p&&rows){
  if(*p=='\n'){buf[used]=0;utf8_text(x,y,buf);used=0;width=0;y+=19;rows--;p++;continue;}
  if(*p<32){p++;continue;}
  n=*p<128?1:((*p&0xf0)==0xe0&&p[1]&&p[2]?3:((*p&0xe0)==0xc0&&p[1]?2:1));
  if(width+(n==1?8:16)>LCD_X_LENGTH-x-8||used+n>=sizeof(buf)){
   buf[used]=0;utf8_text(x,y,buf);used=0;width=0;y+=19;rows--;if(!rows)break;
  }
  memcpy(buf+used,p,n);used+=n;width+=n==1?8:16;p+=n;
 }
 if(used&&rows){buf[used]=0;utf8_text(x,y,buf);}
}
static void header(void){char v[12];area(0,0,LCD_X_LENGTH,21,UI_GREEN);LCD_SetColors(WHITE,UI_GREEN);utf8_text(8,3,"\351\232\217\345\243\260");utf8_text(80,3,"\346\243\200\347\264\242");snprintf(v,sizeof(v),"%u%%",volume);ILI9341_DispString_EN(128,3,v);utf8_text(192,3,"\347\231\273\345\275\225");
 button(0,22,80,27,"\345\234\250\347\272\277",source==0);button(80,22,80,27,"\345\226\234\346\254\242",source==1);button(160,22,80,27,"\346\234\254\345\234\260",source==2);
}
static void footer(void){const char* modes[]={"\351\241\272\345\272\217","\345\276\252\347\216\257","\345\215\225\351\246\226"};
 button(0,272,80,24,"\344\270\212\351\246\226",0);button(80,272,80,24,playing?(paused?"\347\273\247\347\273\255":"\346\232\202\345\201\234") : "\346\222\255\346\224\276",1);button(160,272,80,24,"\344\270\213\351\246\226",0);
 button(0,296,60,24,"\345\207\217\351\237\263",0);button(60,296,60,24,modes[mode%3],0);button(120,296,60,24,"\345\212\240\351\237\263",0);button(180,296,60,24,"\345\201\234\346\255\242",0);
}
static void drawList(void){unsigned i;char number[12];view=0;header();area(0,50,LCD_X_LENGTH,222,BG);
 for(i=0;i<count;i++){unsigned y=52+i*24;area(4,y,232,23,i&1?0xEF7C:BG);LCD_SetColors(GRAY,i&1?0xEF7C:BG);snprintf(number,sizeof(number),"%u",page*8+i+1);ILI9341_DispString_EN(7,y+3,number);LCD_SetColors(FG,i&1?0xEF7C:BG);utf8_text(34,y+3,titles[i]);}
 if(!count){LCD_SetColors(GRAY,BG);wrapText(12,80,source==2?"\346\240\271\347\233\256\345\275\225\346\234\252\346\211\276\345\210\260 MP3/WAV\343\200\202\347\202\271\345\207\273\346\234\254\345\234\260\351\207\215\350\257\273\357\274\214\351\241\266\351\203\250\346\243\200\347\264\242\345\217\257\350\276\223\345\205\245\345\205\263\351\224\256\350\257\215\343\200\202":source==1?"\347\202\271\345\207\273\345\226\234\346\254\242\345\220\214\346\255\245\346\255\214\345\215\225\357\274\233\350\257\267\345\205\210\345\234\250\345\217\263\344\270\212\350\247\222\347\231\273\345\275\225\351\205\267\347\213\227\350\264\246\345\217\267\343\200\202":"\347\202\271\345\207\273\345\234\250\347\272\277\350\276\223\345\205\245\345\205\263\351\224\256\350\257\215\357\274\214\346\210\226\351\200\211\346\213\251\344\270\255\346\226\207\345\277\253\346\215\267\350\257\215\343\200\202",6);}
 button(0,246,80,26,"\344\270\212\351\241\265",0);button(80,246,80,26,"\346\222\255\346\224\276\351\241\265",0);button(160,246,80,26,"\344\270\213\351\241\265",0);footer();
}
static void drawLyrics(void){if(view!=1)return;area(8,108,224,84,BG);LCD_SetColors(FG,BG);wrapText(10,110,lyrics[0]?lyrics:"\346\232\202\346\227\240\345\220\214\346\255\245\346\255\214\350\257\215",4);}
static void drawProgress(void){char t[48];unsigned pixels=0;if(view!=1)return;
 area(8,196,224,48,BG);LCD_SetColors(GRAY,BG);utf8_text(10,197,seeking?"\346\255\243\345\234\250\345\256\232\344\275\215\357\274\214\350\257\267\347\250\215\345\200\231":"\347\202\271\345\207\273\346\210\226\346\213\226\345\212\250\350\277\233\345\272\246\346\235\241\345\256\232\344\275\215");
 area(12,218,216,6,0xD69A);if(duration){uint32_t pos=elapsed>duration?duration:elapsed;pixels=(unsigned)((uint64_t)pos*216/duration);}if(pixels)area(12,218,pixels,6,UI_GREEN);
 snprintf(t,sizeof(t),"%lu:%02lu / %s%lu:%02lu",(unsigned long)(elapsed/60000),(unsigned long)(elapsed/1000%60),estimated?"~":"",(unsigned long)(duration/60000),(unsigned long)(duration/1000%60));LCD_SetColors(FG,BG);ILI9341_DispString_EN(12,228,t);
}
static void drawNow(void){view=1;header();area(0,50,LCD_X_LENGTH,222,BG);LCD_SetColors(FG,BG);wrapText(10,55,nowTitle[0]?nowTitle:"\351\200\211\346\213\251\344\270\200\351\246\226\346\255\214\346\233\262",2);LCD_SetColors(GRAY,BG);utf8_text(10,94,"\345\220\214\346\255\245\346\255\214\350\257\215");drawLyrics();drawProgress();button(0,246,120,26,"\350\277\224\345\233\236\345\210\227\350\241\250",0);button(120,246,120,26,"\346\222\255\346\224\276\346\250\241\345\274\217",0);footer();}
static void keyField(void){const char* text=keyText;size_t len=strlen(text);if(len>20)text+=len-20;area(0,50,180,30,BG);LCD_SetColors(FG,BG);utf8_text(8,55,text[0]?text:"English / 0-9");button(180,50,60,30,"\350\277\224\345\233\236",0);}
static void keyboard(void){unsigned row,col;char key[2]={0,0};const char* rows[]={"qwertyuiop","asdfghjkl","zxcvbnm","0123456789"};view=2;header();area(0,50,LCD_X_LENGTH,222,BG);LCD_SetColors(FG,BG);utf8_text(8,55,keyText[0]?keyText:"\350\213\261\346\226\207 / \346\225\260\345\255\227\346\243\200\347\264\242");
 for(row=0;row<4;row++)for(col=0;rows[row][col];col++){key[0]=rows[row][col];button(col*24,84+row*31,24,29,key,0);}
 button(0,210,60,31,"\345\260\217\350\213\271\346\236\234",0);button(60,210,60,31,"\345\260\217\346\230\237\346\230\237",0);button(120,210,60,31,"\350\275\273\351\237\263\344\271\220",0);button(180,210,60,31,"\351\222\242\347\220\264",0);
 button(0,244,80,28,"\351\200\200\346\240\274",0);button(80,244,80,28,"\346\270\205\347\251\272",0);button(160,244,80,28,"\346\220\234\347\264\242",1);footer();
}
static int decodeHex(const char* hex,char* dest,unsigned capacity){unsigned i,n=(unsigned)strlen(hex);int a,b;if(n%2||n/2>=capacity)return 0;for(i=0;i<n/2;i++){a=hexval(hex[i*2]);b=hexval(hex[i*2+1]);if(a<0||b<0)return 0;dest[i]=(char)(a*16+b);}dest[n/2]=0;return 1;}
static volatile char line[1200];
static volatile uint16_t pos;
static volatile uint8_t pending, overflow, rxError;
static int hexval(char c) {
 if(c>='0'&&c<='9')return c-'0';
 if(c>='A'&&c<='F')return c-'A'+10;
 if(c>='a'&&c<='f')return c-'a'+10;
 return -1;
}
void USART3_IRQHandler(void) {
 char c;
 if(USART_GetFlagStatus(USART3,USART_FLAG_ORE)!=RESET) {
  (void)USART3->SR;(void)USART3->DR; pos=0;overflow=1;rxError=1;
 }
 if(USART_GetITStatus(USART3,USART_IT_RXNE)!=RESET) {
  c=(char)USART_ReceiveData(USART3);
  if(pending)return;
  if(c=='\n') {
   if(!overflow){line[pos]=0;pending=1;}
   pos=0;overflow=0;
  } else if(c!='\r') {
   if(pos<sizeof(line)-1)line[pos++]=c;
   else {overflow=1;rxError=1;}
  }
 }
}
void Player_Reply(const char* s) {
 while(*s) {
  while(USART_GetFlagStatus(USART3,USART_FLAG_TXE)==RESET) {}
  USART_SendData(USART3,(uint16_t)*s++);
 }
}
static void utf8_text(unsigned x,unsigned y,const char* text) {
 const unsigned char* p=(const unsigned char*)text;
 unsigned cp,lo,hi,mid,row,col;
 while(*p && x+16<LCD_X_LENGTH) {
  if(*p<32){p++;continue;}
  if(*p<128){ILI9341_DispChar_EN(x,y,(char)*p++);x+=8;continue;}
  if((p[0]&0xf0)==0xe0 && p[1] && p[2]) {cp=((p[0]&15)<<12)|((p[1]&63)<<6)|(p[2]&63);p+=3;}
  else if((p[0]&0xe0)==0xc0 && p[1]){cp=((p[0]&31)<<6)|(p[1]&63);p+=2;}
  else {p++;continue;}
  {char punctuation=0;
   if(cp==0x3002)punctuation='.';else if(cp==0xff0c||cp==0x3001)punctuation=',';
   else if(cp==0xff01)punctuation='!';else if(cp==0xff1f)punctuation='?';
   else if(cp==0xff1a)punctuation=':';else if(cp==0xff1b)punctuation=';';
   else if(cp==0x2014)punctuation='-';else if(cp==0x2026)punctuation='.';
   else if(cp==0x2018||cp==0x2019)punctuation=39;else if(cp==0x201c||cp==0x201d)punctuation=34;
   if(punctuation){ILI9341_DispChar_EN(x,y,punctuation);x+=8;continue;}
  }
  lo=0;hi=ui_hanzi_count;
  while(lo<hi){mid=(lo+hi)/2;if(ui_hanzi_codes[mid]<cp)lo=mid+1;else hi=mid;}
  if(lo<ui_hanzi_count && ui_hanzi_codes[lo]==cp) {
   for(row=0;row<16;row++)for(col=0;col<16;col++)
    if(ui_hanzi_bits[lo][row*2+col/8]&(128>>(col%8)))ILI9341_SetPointPixel(x+col,y+row);
  } else ILI9341_DispChar_EN(x,y,'?');
  x+=16;
 }
}
static void keys_poll(void) {
 static uint8_t raw[2],stable[2];
 static uint32_t changed[2],pressed[2];
 uint8_t v;
 unsigned i;
 uint32_t now=ui_ms;
 for(i=0;i<2;i++) {
  v=(uint8_t)(i?GPIO_ReadInputDataBit(GPIOC,GPIO_Pin_13):GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0));
  if(v!=raw[i]){raw[i]=v;changed[i]=now;}
  if(v!=stable[i] && (uint32_t)(now-changed[i])>=25) {
   stable[i]=v;
   if(v)pressed[i]=now;
   else if((uint32_t)(now-pressed[i])>=1000)Player_Reply(i?"L\n":"R\n");
   else Player_Reply(i?"P\n":"N\n");
  }
 }
}
void QR_UI_Init(void){
 GPIO_InitTypeDef g;USART_InitTypeDef u;SysTick_Config(SystemCoreClock/1000);ILI9341_Init();ILI9341_GramScan(6);
 area(0,0,LCD_X_LENGTH,LCD_Y_LENGTH,BG);Touch_Init();
 RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB|RCC_APB2Periph_GPIOA|RCC_APB2Periph_GPIOC,ENABLE);
 GPIO_StructInit(&g);g.GPIO_Pin=GPIO_Pin_0;g.GPIO_Mode=GPIO_Mode_IPD;GPIO_Init(GPIOA,&g);g.GPIO_Pin=GPIO_Pin_13;GPIO_Init(GPIOC,&g);

 RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3,ENABLE);GPIO_StructInit(&g);g.GPIO_Pin=GPIO_Pin_11;g.GPIO_Mode=GPIO_Mode_IPU;GPIO_Init(GPIOB,&g);g.GPIO_Pin=GPIO_Pin_10;g.GPIO_Mode=GPIO_Mode_AF_PP;GPIO_Init(GPIOB,&g);
 USART_StructInit(&u);u.USART_BaudRate=230400;u.USART_Mode=USART_Mode_Rx|USART_Mode_Tx;USART_Init(USART3,&u);USART_ITConfig(USART3,USART_IT_RXNE,ENABLE);NVIC_SetPriority(USART3_IRQn,0);NVIC_EnableIRQ(USART3_IRQn);USART_Cmd(USART3,ENABLE);
 if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0)&&GPIO_ReadInputDataBit(GPIOC,GPIO_Pin_13))Touch_Calibrate();
 while(!Touch_IsCalibrated())Touch_Calibrate();
 /* Flash programming can stall RX IRQs. Discard calibration-time probes and partial lines. */
 __disable_irq();(void)USART3->SR;(void)USART3->DR;pos=0;pending=0;overflow=0;rxError=0;__enable_irq();
 drawList();
 Player_Reply(Touch_IsPersisted()?"CAL_SAVED:1\n":"CAL_SAVED:0\n");
}
void QR_UI_Poll(void){
 static char local[1200];static uint8_t bitmap[407];unsigned n,x,y,k,bytes,scale,ox,oy,index,base;int a,b;char* data;char ack[48];
 keys_poll();touch_poll();Storage_Poll();if(rxError){rxError=0;Player_Reply("QR_ERR:RX_OVERRUN\n");}
 if(!pending)return;strcpy(local,(const char*)line);__DMB();pending=0;
 if(!strcmp(local,"HI")){Player_Reply("UI_READY:24\n");return;}
 if(Storage_Command(local))return;
 if(!strncmp(local,"LB:",3)){
  if(sscanf(local+3,"%u:%u:%u:%u:%u",&source,&page,&total,&count,&hasNext)!=5){Player_Reply("UI_ERR\n");return;}
  if(source>2)source=0;if(count>8)count=8;memset(titles,0,sizeof(titles));drawList();Player_Reply("UI_OK\n");return;
 }
 if(!strncmp(local,"LT:",3)){
  index=(unsigned)atoi(local+3);data=strchr(local+3,':');if(!data||index>=8||!decodeHex(data+1,titles[index],sizeof(titles[index]))){Player_Reply("UI_ERR\n");return;}
  if(view==0&&index<count){y=52+index*24;area(34,y,202,23,index&1?0xEF7C:BG);LCD_SetColors(FG,index&1?0xEF7C:BG);utf8_text(34,y+3,titles[index]);}
  Player_Reply("UI_OK\n");return;
 }
 if(!strncmp(local,"NP:",3)){if(decodeHex(local+3,nowTitle,sizeof(nowTitle))){lyrics[0]=0;elapsed=0;duration=0;drawNow();}Player_Reply("UI_OK\n");return;}
 if(!strncmp(local,"PG:",3)){
  unsigned oldPause=paused,oldVolume=volume,oldMode=mode,oldPlaying=playing;
  if(sscanf(local+3,"%u:%u:%u:%u:%u:%u:%u:%u:%u",&elapsed,&duration,&paused,&volume,&mode,&estimated,&seeking,&base,&playing)!=9){Player_Reply("UI_ERR\n");return;}
  elapsed=base+Audio_PlayedSamples()/32;
  snprintf(ack,sizeof(ack),"UI_OK:%lu\n",(unsigned long)Audio_PlayedSamples());Player_Reply(ack);
  drawProgress();if(oldPause!=paused||oldMode!=mode||oldPlaying!=playing)footer();if(oldVolume!=volume)header();return;
 }
 if(!strncmp(local,"LY:",3)){if(decodeHex(local+3,lyrics,sizeof(lyrics))){Player_Reply("UI_OK\n");drawLyrics();}else Player_Reply("UI_ERR\n");return;}
 if(!strncmp(local,"MSG:",4)){char message[128];if(decodeHex(local+4,message,sizeof(message))){area(0,50,LCD_X_LENGTH,194,BG);LCD_SetColors(FG,BG);wrapText(10,72,message,8);}Player_Reply("UI_OK\n");return;}
 if(!strncmp(local,"S:",2)){
  if(!strncmp(local+2,"Searching",9)){area(0,0,180,21,UI_GREEN);LCD_SetColors(WHITE,UI_GREEN);utf8_text(8,3,"\346\255\243\345\234\250\346\220\234\347\264\242\346\255\214\346\233\262");}
  else if(!strncmp(local+2,"Getting",7)){area(0,0,180,21,UI_GREEN);LCD_SetColors(WHITE,UI_GREEN);utf8_text(8,3,"\346\255\243\345\234\250\350\216\267\345\217\226\351\237\263\344\271\220");}
  else if(!strncmp(local+2,"Finished",8)||!strncmp(local+2,"Stopped",7)){paused=1;playing=0;footer();}
  else if(!strncmp(local+2,"Login saved",11)||!strncmp(local+2,"Logged in",9)){drawList();area(0,0,180,21,UI_GREEN);LCD_SetColors(WHITE,UI_GREEN);utf8_text(8,3,"\347\231\273\345\275\225\346\210\220\345\212\237");}
  else if(!strncmp(local+2,"Confirm",7)){area(0,0,240,25,WHITE);LCD_SetColors(FG,WHITE);utf8_text(8,4,"\350\257\267\345\234\250\346\211\213\346\234\272\347\241\256\350\256\244\347\231\273\345\275\225");}
  else if(!strncmp(local+2,"QR expired",10)){area(0,0,240,25,WHITE);LCD_SetColors(FG,WHITE);utf8_text(8,4,"\344\272\214\347\273\264\347\240\201\345\267\262\350\277\207\346\234\237\357\274\214\350\257\267\345\210\267\346\226\260");}
  else if(!strncmp(local+2,"Login request failed",20)){area(0,0,180,21,UI_GREEN);LCD_SetColors(WHITE,UI_GREEN);utf8_text(8,3,"\347\231\273\345\275\225\345\244\261\350\264\245\357\274\214\350\257\267\346\243\200\346\237\245\347\275\221\347\273\234");}
  Player_Reply("UI_OK\n");return;
 }
 if(strncmp(local,"Q:",2))return;
 n=(unsigned)atoi(local+2);data=strchr(local+2,':');if(!data||n<21||n>57||((n-21)%4)){Player_Reply("QR_ERR:SIZE\n");return;}data++;
 bytes=(n*n+7)/8;if(strlen(data)!=bytes*2){Player_Reply("QR_ERR:LENGTH\n");return;}
 for(k=0;k<bytes;k++){a=hexval(data[k*2]);b=hexval(data[k*2+1]);if(a<0||b<0){Player_Reply("QR_ERR:HEX\n");return;}bitmap[k]=(uint8_t)(a*16+b);}
 view=3;scale=LCD_X_LENGTH/(n+8);if(scale>240/(n+8))scale=240/(n+8);if(!scale)return;
 ox=(LCD_X_LENGTH-(n+8)*scale)/2+4*scale;oy=35+(240-(n+8)*scale)/2+4*scale;
 area(0,0,LCD_X_LENGTH,LCD_Y_LENGTH,WHITE);LCD_SetColors(BLACK,WHITE);utf8_text(8,7,"\344\275\277\347\224\250\351\205\267\347\213\227\346\211\253\347\240\201\347\231\273\345\275\225");
 for(y=0;y<n;y++)for(x=0;x<n;x++){k=y*n+x;if(bitmap[k/8]&(1<<(k%8)))ILI9341_DrawRectangle(ox+x*scale,oy+y*scale,scale,scale,1);}
 button(0,288,120,32,"\350\277\224\345\233\236",0);button(120,288,120,32,"\345\210\267\346\226\260\347\240\201",0);Player_Reply("QR_OK\n");
}
/* Immediate visual feedback; no command is issued until the finger is released. */
static unsigned pressId,pressView,pressSource,pressPage,feedbackActive,pressX,pressY,pressW,pressH;
static unsigned hitControl(unsigned x,unsigned y,unsigned* rx,unsigned* ry,unsigned* rw,unsigned* rh){
 unsigned i,row,col;
 const char* keys[]={"qwertyuiop","asdfghjkl","zxcvbnm","0123456789"};
 *rx=0;*ry=0;*rw=0;*rh=0;
 if(view==3){if(y<288)return 0;*rx=x<120?0:120;*ry=288;*rw=120;*rh=32;return x<120?80:81;}
 if(y<21){*rx=(x/80)*80;*ry=0;*rw=80;*rh=21;return x<80?1:x<160?3:2;}
 if(y>=22&&y<49){i=x/80;*rx=i*80;*ry=22;*rw=80;*rh=27;return 10+i;}
 if(y>=296){i=x/60;*rx=i*60;*ry=296;*rw=60;*rh=24;return 30+i;}
 if(y>=272){i=x/80;*rx=i*80;*ry=272;*rw=80;*rh=24;return 20+i;}
 if(view==2){
  if(y>=50&&y<80&&x>=180){*rx=180;*ry=50;*rw=60;*rh=30;return 50;}
  if(y>=84&&y<208){row=(y-84)/31;col=x/24;if(row<4&&col<strlen(keys[row])){*rx=col*24;*ry=84+row*31;*rw=24;*rh=29;return 100+row*10+col;}return 0;}
  if(y>=210&&y<241){i=x/60;*rx=i*60;*ry=210;*rw=60;*rh=31;return 150+i;}
  if(y>=244){i=x/80;*rx=i*80;*ry=244;*rw=80;*rh=28;return 160+i;}return 0;
 }
 if(view==1){
  if(y>=202&&y<244&&duration){*rx=10;*ry=214;*rw=220;*rh=14;return 200;}
  if(y>=246){i=x/120;*rx=i*120;*ry=246;*rw=120;*rh=26;return 201+i;}return 0;
 }
 if(y>=246){i=x/80;if((i==0&&!page)||(i==2&&!hasNext))return 0;*rx=i*80;*ry=246;*rw=80;*rh=26;return 210+i;}
 if(y>=52&&y<244){i=(y-52)/24;if(i<count){*rx=4;*ry=52+i*24;*rw=232;*rh=23;return 220+i;}}
 return 0;
}
static void restorePress(void){
 if(!feedbackActive)return;feedbackActive=0;
 if(view==0)drawList();else if(view==1)drawNow();else if(view==2)keyboard();
 else {button(0,288,120,32,"\350\277\224\345\233\236",0);button(120,288,120,32,"\345\210\267\346\226\260\347\240\201",0);}
}
static void showPress(void){LCD_SetColors(0xFC20,BG);ILI9341_DrawRectangle(pressX+1,pressY+1,pressW-2,pressH-2,0);ILI9341_DrawRectangle(pressX+2,pressY+2,pressW-4,pressH-4,0);feedbackActive=1;}
static void touch_poll(void){
 unsigned x,y,index,col,row,len,hx,hy,hw,hh,hit;int phase;char event[120];const char* keys[]={"qwertyuiop","asdfghjkl","zxcvbnm","0123456789"};
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
 if(y<21){if(x>=160)Player_Reply("L\n");else if(x>=80)keyboard();else Player_Reply("UI:REFRESH\n");return;}
 if(y>=22&&y<49){index=x/80;if(index==source&&source==0){keyboard();return;}if(index==source&&source==1){Player_Reply("UI:REFRESH\n");return;}snprintf(event,sizeof(event),"UI:SOURCE:%u\n",index);Player_Reply(event);return;}
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
