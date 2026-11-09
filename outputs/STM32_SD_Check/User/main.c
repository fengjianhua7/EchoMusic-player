#include "stm32f10x.h"
#include "lcd/bsp_ili9341_lcd.h"
#include "sdio/bsp_sdio_sdcard.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

extern SD_CardInfo SDCardInfo;
static FATFS fs;
static FIL file;
static uint16_t samples[512];
static uint8_t input[512];
static volatile uint8_t refill;
static uint32_t remaining;
static int readError;
static uint16_t le16(const uint8_t* b){return b[0]|((uint16_t)b[1]<<8);}
static uint32_t le32(const uint8_t* b){return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static void message(unsigned y,const char* text) {
 LCD_SetColors(BLACK,WHITE);ILI9341_Clear(0,y,LCD_X_LENGTH,20);
 ILI9341_DispString_EN(4,y,(char*)text);
}
void DMA2_Channel3_IRQHandler(void) {
 if(DMA_GetITStatus(DMA2_IT_HT3)){DMA_ClearITPendingBit(DMA2_IT_HT3);refill|=1;}
 if(DMA_GetITStatus(DMA2_IT_TC3)){DMA_ClearITPendingBit(DMA2_IT_TC3);refill|=2;}
}
static int fill(unsigned half) {
 UINT got=0;unsigned i,n=remaining>512?512:(unsigned)remaining;
 memset(input,0,sizeof(input));
 if(n && (f_read(&file,input,n,&got)!=FR_OK || got!=n)){readError=1;remaining=0;n=0;}
 else remaining-=n;
 for(i=0;i<256;i++) {
  int32_t value=(int16_t)le16(input+2*i);
  samples[half*256+i]=(uint16_t)((value/4+32768)/16);
 }
 return !readError;
}
static void dacStart(void) {
 GPIO_InitTypeDef g;DMA_InitTypeDef d;DAC_InitTypeDef a;TIM_TimeBaseInitTypeDef t;RCC_ClocksTypeDef c;
 uint32_t clock;
 RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
 RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC|RCC_APB1Periph_TIM6,ENABLE);
 RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA2,ENABLE);
 GPIO_StructInit(&g);g.GPIO_Pin=GPIO_Pin_4;g.GPIO_Mode=GPIO_Mode_AIN;GPIO_Init(GPIOA,&g);
 RCC_GetClocksFreq(&c);clock=c.PCLK1_Frequency;if(c.PCLK1_Frequency!=c.HCLK_Frequency)clock*=2;
 TIM_TimeBaseStructInit(&t);t.TIM_Period=(uint16_t)(clock/32000-1);TIM_TimeBaseInit(TIM6,&t);
 TIM_SelectOutputTrigger(TIM6,TIM_TRGOSource_Update);
 DAC_StructInit(&a);a.DAC_Trigger=DAC_Trigger_T6_TRGO;a.DAC_OutputBuffer=DAC_OutputBuffer_Enable;DAC_Init(DAC_Channel_1,&a);
 DAC_SetChannel1Data(DAC_Align_12b_R,2048);
 DMA_DeInit(DMA2_Channel3);DMA_StructInit(&d);
 d.DMA_PeripheralBaseAddr=(uint32_t)&DAC->DHR12R1;d.DMA_MemoryBaseAddr=(uint32_t)samples;
 d.DMA_DIR=DMA_DIR_PeripheralDST;d.DMA_BufferSize=512;d.DMA_MemoryInc=DMA_MemoryInc_Enable;
 d.DMA_PeripheralDataSize=DMA_PeripheralDataSize_HalfWord;d.DMA_MemoryDataSize=DMA_MemoryDataSize_HalfWord;
 d.DMA_Mode=DMA_Mode_Circular;d.DMA_Priority=DMA_Priority_High;DMA_Init(DMA2_Channel3,&d);
 DMA_ITConfig(DMA2_Channel3,DMA_IT_HT|DMA_IT_TC,ENABLE);NVIC_EnableIRQ(DMA2_Channel3_IRQn);
 DMA_Cmd(DMA2_Channel3,ENABLE);DAC_Cmd(DAC_Channel_1,ENABLE);DAC_DMACmd(DAC_Channel_1,ENABLE);TIM_Cmd(TIM6,ENABLE);
}
static int waveHeader(void) {
 uint8_t h[16];UINT got;DWORD size;int format=0;
 if(f_read(&file,h,12,&got)!=FR_OK||got!=12||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4))return 0;
 while(f_tell(&file)+8<=f_size(&file)) {
  if(f_read(&file,h,8,&got)!=FR_OK||got!=8)return 0;
  size=le32(h+4);if(size>f_size(&file)-f_tell(&file))return 0;
  if(!memcmp(h,"fmt ",4)) {
   if(size<16||f_read(&file,h,16,&got)!=FR_OK||got!=16)return 0;
   format=le16(h)==1 && le16(h+2)==1 && le32(h+4)==32000 && le16(h+12)==2 && le16(h+14)==16;
   if(!format)return 0;
   if(f_lseek(&file,f_tell(&file)+size-16+(size&1))!=FR_OK)return 0;
  } else if(!memcmp(h,"data",4)) {remaining=size;return format && !(size&1);}
  else if(f_lseek(&file,f_tell(&file)+size+(size&1))!=FR_OK)return 0;
 }
 return 0;
}
static void playTest(void) {
 unsigned tail=0;
 readError=0;remaining=0;refill=0;
 if(f_open(&file,"0:/TEST.WAV",FA_READ)!=FR_OK){message(LCD_Y_LENGTH-40,"Copy TEST.WAV to card root");return;}
 if(!waveHeader()){f_close(&file);message(LCD_Y_LENGTH-40,"Need mono PCM16 32000Hz WAV");return;}
 fill(0);fill(1);dacStart();message(LCD_Y_LENGTH-40,"Local WAV playing...");
 while(!readError && tail<2) {
  if(refill&1){__disable_irq();refill&=(uint8_t)~1;__enable_irq();if(!remaining)tail++;fill(0);}
  if(refill&2){__disable_irq();refill&=(uint8_t)~2;__enable_irq();if(!remaining)tail++;fill(1);}
 }
 TIM_Cmd(TIM6,DISABLE);DAC_DMACmd(DAC_Channel_1,DISABLE);DMA_Cmd(DMA2_Channel3,DISABLE);
 DAC_Cmd(DAC_Channel_1,DISABLE);DAC_SoftwareTriggerCmd(DAC_Channel_1,DISABLE);
 f_close(&file);message(LCD_Y_LENGTH-40,readError?"Card read failed":"Finished - K2 to replay");
}
int main(void) {
 FRESULT r;DIR dir;FILINFO info;char text[48];unsigned y=62,count=0;int pressed=0;
 GPIO_InitTypeDef g;
 ILI9341_Init();ILI9341_GramScan(6);LCD_SetColors(BLACK,WHITE);ILI9341_Clear(0,0,LCD_X_LENGTH,LCD_Y_LENGTH);
 message(4,"TF card read-only test");message(24,"Mounting...");
 r=f_mount(&fs,"0:",1);
 if(r!=FR_OK){snprintf(text,sizeof(text),"Mount failed: FatFs %u",(unsigned)r);message(24,text);message(46,"No format/erase performed");while(1){}}
 snprintf(text,sizeof(text),"Card OK: %lu MB",(unsigned long)(SDCardInfo.CardCapacity/1024/1024));message(24,text);
 message(44,"Root files (8.3 names):");
 if(f_opendir(&dir,"0:/")==FR_OK) {
  while(count<5 && f_readdir(&dir,&info)==FR_OK && info.fname[0]) {
   if(info.fattrib&AM_DIR)continue;
   snprintf(text,sizeof(text),"%s %luB",info.fname,(unsigned long)info.fsize);message(y,text);y+=20;count++;
  }
  f_closedir(&dir);
 }
 message(LCD_Y_LENGTH-20,"K2: play TEST.WAV, read-only");
 RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC,ENABLE);GPIO_StructInit(&g);g.GPIO_Mode=GPIO_Mode_IPD;g.GPIO_Pin=GPIO_Pin_13;GPIO_Init(GPIOC,&g);
 while(1) {
  if(GPIO_ReadInputDataBit(GPIOC,GPIO_Pin_13)){pressed=1;}
  else if(pressed){pressed=0;playTest();}
 }
}

