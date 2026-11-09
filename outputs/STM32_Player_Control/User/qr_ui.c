#include "stm32f10x.h"
#include "lcd/bsp_ili9341_lcd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
extern const uint16_t ui_hanzi_codes[];
extern const uint8_t ui_hanzi_bits[][32];
extern const unsigned ui_hanzi_count;
volatile uint32_t ui_ms;
static void keys_poll(void);
static int list_message(char* text);

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
static void ui_reply(const char* s) {
 while(*s) {
  while(USART_GetFlagStatus(USART3,USART_FLAG_TXE)==RESET) {}
  USART_SendData(USART3,(uint16_t)*s++);
 }
}
void QR_UI_Init(void) {
 GPIO_InitTypeDef g;
 USART_InitTypeDef u;
 SysTick_Config(SystemCoreClock/1000);
 ILI9341_Init();ILI9341_GramScan(6);
 LCD_SetColors(BLACK,WHITE);
 ILI9341_Clear(0,0,LCD_X_LENGTH,LCD_Y_LENGTH);
 ILI9341_DispString_EN(8,8,"Waiting for ESP32 login");
 RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB|RCC_APB2Periph_GPIOA|RCC_APB2Periph_GPIOC,ENABLE);
 GPIO_StructInit(&g);g.GPIO_Pin=GPIO_Pin_0;g.GPIO_Mode=GPIO_Mode_IPD;GPIO_Init(GPIOA,&g);
 g.GPIO_Pin=GPIO_Pin_13;GPIO_Init(GPIOC,&g);
 RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3,ENABLE);
 GPIO_StructInit(&g);g.GPIO_Pin=GPIO_Pin_11;g.GPIO_Mode=GPIO_Mode_IPU;GPIO_Init(GPIOB,&g);
 g.GPIO_Pin=GPIO_Pin_10;g.GPIO_Mode=GPIO_Mode_AF_PP;GPIO_Init(GPIOB,&g);
 USART_StructInit(&u);u.USART_BaudRate=38400;u.USART_Mode=USART_Mode_Rx|USART_Mode_Tx;
 USART_Init(USART3,&u);USART_ITConfig(USART3,USART_IT_RXNE,ENABLE);
 NVIC_SetPriority(USART3_IRQn,0);
 NVIC_EnableIRQ(USART3_IRQn);USART_Cmd(USART3,ENABLE);
}
void QR_UI_Poll(void) {
 static char local[1200];
 char* data;
 unsigned n,x,y,k,byteCount,scale,ox,oy;
 int hi,lo;
 static uint8_t bitmap[407];
 keys_poll();
 if(rxError){rxError=0;ui_reply("QR_ERR:RX_OVERRUN\n");}
 if(!pending)return;
 strcpy(local,(const char*)line);__DMB();pending=0;
 if(list_message(local))return;
 if(local[0]=='S'&&local[1]==':') {
  local[38]=0;
  LCD_SetColors(BLACK,WHITE);ILI9341_Clear(0,0,LCD_X_LENGTH,24);
  ILI9341_DispString_EN(4,4,local+2);return;
 }
 if(local[0]!='Q'||local[1]!=':')return;
 n=(unsigned)atoi(local+2);data=strchr(local+2,':');if(!data){ui_reply("QR_ERR:HEADER\n");return;}data++;
 if(n<21||n>57||((n-21)%4)){ui_reply("QR_ERR:SIZE\n");return;}
 byteCount=(n*n+7)/8;
 if(strlen(data)!=byteCount*2){ui_reply("QR_ERR:LENGTH\n");return;}
 for(k=0;k<byteCount;k++) {
  hi=hexval(data[k*2]);lo=hexval(data[k*2+1]);if(hi<0||lo<0){ui_reply("QR_ERR:HEX\n");return;}
  bitmap[k]=(uint8_t)(hi*16+lo);
 }
 scale=LCD_X_LENGTH/(n+8);
 if(scale>(LCD_Y_LENGTH-32)/(n+8))scale=(LCD_Y_LENGTH-32)/(n+8);
 if(!scale)return;
 ox=(LCD_X_LENGTH-(n+8)*scale)/2+4*scale;
 oy=28+(LCD_Y_LENGTH-28-(n+8)*scale)/2+4*scale;
 LCD_SetColors(BLACK,WHITE);ILI9341_Clear(0,0,LCD_X_LENGTH,LCD_Y_LENGTH);
 ILI9341_DispString_EN(4,4,"Scan with Kugou app");
 for(y=0;y<n;y++)for(x=0;x<n;x++) {
  k=y*n+x;
  if(bitmap[k/8]&(1<<(k%8)))ILI9341_DrawRectangle(ox+x*scale,oy+y*scale,scale,scale,1);
 }
 ui_reply("QR_OK\n");
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
   else if((uint32_t)(now-pressed[i])>=1000)ui_reply(i?"L\n":"R\n");
   else ui_reply(i?"P\n":"N\n");
  }
 }
}
static void utf8_text(unsigned x,unsigned y,const char* text) {
 const unsigned char* p=(const unsigned char*)text;
 unsigned cp,lo,hi,mid,row,col;
 while(*p && x+16<LCD_X_LENGTH) {
  if(*p<128){ILI9341_DispChar_EN(x,y,(char)*p++);x+=8;continue;}
  if((p[0]&0xf0)==0xe0 && p[1] && p[2]) {cp=((p[0]&15)<<12)|((p[1]&63)<<6)|(p[2]&63);p+=3;}
  else if((p[0]&0xe0)==0xc0 && p[1]){cp=((p[0]&31)<<6)|(p[1]&63);p+=2;}
  else {p++;continue;}
  lo=0;hi=ui_hanzi_count;
  while(lo<hi){mid=(lo+hi)/2;if(ui_hanzi_codes[mid]<cp)lo=mid+1;else hi=mid;}
  if(lo<ui_hanzi_count && ui_hanzi_codes[lo]==cp) {
   for(row=0;row<16;row++)for(col=0;col<16;col++)
    if(ui_hanzi_bits[lo][row*2+col/8]&(128>>(col%8)))ILI9341_SetPointPixel(x+col,y+row);
  } else ILI9341_DispChar_EN(x,y,'?');
  x+=16;
 }
}
static int list_message(char* text) {
 static char titles[5][128];
 static unsigned count,selected;
 unsigned i,j,index;
 char* data;
 int a,b;
 char label[4];
 if(text[0]=='B'&&text[1]==':') {
  count=(unsigned)atoi(text+2);if(count>5)count=5;selected=0;
  memset(titles,0,sizeof(titles));
  LCD_SetColors(BLACK,WHITE);ILI9341_Clear(0,24,LCD_X_LENGTH,LCD_Y_LENGTH-24);
  ILI9341_DispString_EN(4,LCD_Y_LENGTH-36,"K1: select  K2: play/pause");
  ILI9341_DispString_EN(4,LCD_Y_LENGTH-18,"Hold K1: search K2: login");
  ui_reply("UI_OK\n");return 1;
 }
 if(text[0]=='T'&&text[1]==':') {
  index=(unsigned)atoi(text+2);data=strchr(text+2,':');if(!data||index>=count)return 1;data++;
  j=(unsigned)strlen(data);if(j%2||j/2>=sizeof(titles[0]))return 1;
  for(i=0;i<j/2;i++){a=hexval(data[2*i]);b=hexval(data[2*i+1]);if(a<0||b<0)return 1;titles[index][i]=(char)(a*16+b);}
  titles[index][j/2]=0;
  LCD_SetColors(BLACK,WHITE);ILI9341_Clear(0,28+index*30,LCD_X_LENGTH,28);
  snprintf(label,sizeof(label),"%u",index+1);ILI9341_DispString_EN(16,30+index*30,label);
  utf8_text(32,30+index*30,titles[index]);ui_reply("UI_OK\n");return 1;
 }
 if(text[0]=='C'&&text[1]==':') {
  selected=(unsigned)atoi(text+2);
  LCD_SetColors(BLACK,WHITE);
  for(i=0;i<count;i++){ILI9341_Clear(0,28+i*30,12,28);if(i==selected)ILI9341_DispChar_EN(2,30+i*30,'>');}
  ui_reply("UI_OK\n");return 1;
 }
 return 0;
}
