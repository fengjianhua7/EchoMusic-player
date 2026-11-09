#include "stm32f10x.h"
#include "lcd/bsp_ili9341_lcd.h"
#include <string.h>
#include <stdlib.h>

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
 ILI9341_Init();ILI9341_GramScan(6);
 LCD_SetColors(BLACK,WHITE);
 ILI9341_Clear(0,0,LCD_X_LENGTH,LCD_Y_LENGTH);
 ILI9341_DispString_EN(8,8,"Waiting for ESP32 login");
 RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
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
 if(rxError){rxError=0;ui_reply("QR_ERR:RX_OVERRUN\n");}
 if(!pending)return;
 strcpy(local,(const char*)line);__DMB();pending=0;
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
