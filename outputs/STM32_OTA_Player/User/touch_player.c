#include "stm32f10x.h"
#include "stm32f10x_flash.h"
#include <string.h>
#include "lcd/bsp_ili9341_lcd.h"
extern volatile uint32_t ui_ms;
/* STM32F103VE: final 2KB Flash page is excluded from the linker region. */
#define CAL_ADDR 0x0807F800UL
#define CAL_MAGIC 0x54432301UL
typedef struct {uint32_t magic,width,height;float ax,bx,cx,ay,by,cy;uint32_t checksum;} Calibration;
static Calibration cal;
static int calibrated,persisted;
static uint32_t calChecksum(const Calibration* c){const uint8_t* p=(const uint8_t*)c;uint32_t h=2166136261UL;unsigned i;for(i=0;i<sizeof(*c)-4;i++)h=(h^p[i])*16777619UL;return h;}
static int loadCalibration(void){const Calibration* saved=(const Calibration*)CAL_ADDR;
 if(saved->magic!=CAL_MAGIC||saved->width!=LCD_X_LENGTH||saved->height!=LCD_Y_LENGTH||saved->checksum!=calChecksum(saved))return 0;
 cal=*saved;return 1;
}
static int saveCalibration(void){unsigned i;FLASH_Status status;const uint32_t* data=(const uint32_t*)&cal;
 cal.magic=CAL_MAGIC;cal.width=LCD_X_LENGTH;cal.height=LCD_Y_LENGTH;cal.checksum=calChecksum(&cal);
 __disable_irq();FLASH_Unlock();FLASH_ClearFlag(FLASH_FLAG_EOP|FLASH_FLAG_PGERR|FLASH_FLAG_WRPRTERR);
 status=FLASH_ErasePage(CAL_ADDR);
 for(i=0;status==FLASH_COMPLETE&&i<sizeof(cal)/4;i++)status=FLASH_ProgramWord(CAL_ADDR+i*4,data[i]);
 FLASH_Lock();__enable_irq();return status==FLASH_COMPLETE&&!memcmp((const void*)CAL_ADDR,&cal,sizeof(cal));
}
int Touch_IsCalibrated(void){return calibrated;}
int Touch_IsPersisted(void){return persisted;}
static void tick(void){volatile unsigned n;for(n=0;n<12;n++){} }
static uint16_t adc(uint8_t command) {
 unsigned i;uint16_t v=0;
 GPIO_ResetBits(GPIOD,GPIO_Pin_13);
 for(i=0;i<8;i++) {
  GPIO_ResetBits(GPIOE,GPIO_Pin_0);
  if(command&128)GPIO_SetBits(GPIOE,GPIO_Pin_2);else GPIO_ResetBits(GPIOE,GPIO_Pin_2);
  tick();GPIO_SetBits(GPIOE,GPIO_Pin_0);tick();command<<=1;
 }
 GPIO_ResetBits(GPIOE,GPIO_Pin_0);tick();
 for(i=0;i<16;i++) {
  GPIO_SetBits(GPIOE,GPIO_Pin_0);tick();
  v=(uint16_t)((v<<1)|(GPIO_ReadInputDataBit(GPIOE,GPIO_Pin_3)?1:0));
  GPIO_ResetBits(GPIOE,GPIO_Pin_0);tick();
 }
 GPIO_SetBits(GPIOD,GPIO_Pin_13);
 return (v>>3)&4095;
}
static int down(void){return GPIO_ReadInputDataBit(GPIOE,GPIO_Pin_4)==Bit_RESET;}
static int raw(unsigned* x,unsigned* y) {
 unsigned a[5],b[5],i,j,v;
 if(!down())return 0;
 for(i=0;i<5;i++){a[i]=adc(0x90);b[i]=adc(0xd0);}
 if(!down())return 0;
 for(i=0;i<5;i++)for(j=i+1;j<5;j++){
  if(a[i]>a[j]){v=a[i];a[i]=a[j];a[j]=v;}
  if(b[i]>b[j]){v=b[i];b[i]=b[j];b[j]=v;}
 }
 *x=a[2];*y=b[2];return 1;
}
void Touch_Init(void) {
 GPIO_InitTypeDef g;
 RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE|RCC_APB2Periph_GPIOD,ENABLE);
 GPIO_StructInit(&g);g.GPIO_Mode=GPIO_Mode_Out_PP;g.GPIO_Speed=GPIO_Speed_10MHz;
 g.GPIO_Pin=GPIO_Pin_0|GPIO_Pin_2;GPIO_Init(GPIOE,&g);
 g.GPIO_Pin=GPIO_Pin_13;GPIO_Init(GPIOD,&g);GPIO_SetBits(GPIOD,GPIO_Pin_13);
 g.GPIO_Mode=GPIO_Mode_IPU;g.GPIO_Pin=GPIO_Pin_3|GPIO_Pin_4;GPIO_Init(GPIOE,&g);
 calibrated=loadCalibration();persisted=calibrated;
}
/* Three calibration points plus a center verification; coefficients persist in Flash. */
static int calibrationPoint(unsigned tx,unsigned ty,unsigned* x,unsigned* y){uint32_t start;
 LCD_SetColors(BLACK,WHITE);ILI9341_Clear(0,0,LCD_X_LENGTH,LCD_Y_LENGTH);
 ILI9341_DispString_EN(8,65,"Tap cross and hold");
 ILI9341_DispString_EN(8,85,"Then release finger");
 ILI9341_DrawLine(tx-9,ty,tx+9,ty);ILI9341_DrawLine(tx,ty-9,tx,ty+9);
 while(down()){}start=ui_ms;
 while((uint32_t)(ui_ms-start)<60000){
  if(raw(x,y)){uint32_t held=ui_ms;unsigned a,b;
   while(down()&&(uint32_t)(ui_ms-held)<100){}
   if(raw(&a,&b)){*x=a;*y=b;while(down()){}return 1;}
  }
 }
 return 0;
}
void Touch_Calibrate(void){
 float rx[3],ry[3],tx[3],ty[3],d;unsigned i,x,y;int px,py;
 Calibration candidate=cal;
 tx[0]=20;ty[0]=24;tx[1]=LCD_X_LENGTH-20;ty[1]=24;tx[2]=LCD_X_LENGTH/2;ty[2]=LCD_Y_LENGTH-24;
 for(i=0;i<3;i++){if(!calibrationPoint((unsigned)tx[i],(unsigned)ty[i],&x,&y))return;rx[i]=(float)x;ry[i]=(float)y;}
 d=rx[0]*(ry[1]-ry[2])+rx[1]*(ry[2]-ry[0])+rx[2]*(ry[0]-ry[1]);if(d<100&&d>-100)return;
 candidate.ax=(tx[0]*(ry[1]-ry[2])+tx[1]*(ry[2]-ry[0])+tx[2]*(ry[0]-ry[1]))/d;
 candidate.bx=(tx[0]*(rx[2]-rx[1])+tx[1]*(rx[0]-rx[2])+tx[2]*(rx[1]-rx[0]))/d;
 candidate.cx=tx[0]-candidate.ax*rx[0]-candidate.bx*ry[0];
 candidate.ay=(ty[0]*(ry[1]-ry[2])+ty[1]*(ry[2]-ry[0])+ty[2]*(ry[0]-ry[1]))/d;
 candidate.by=(ty[0]*(rx[2]-rx[1])+ty[1]*(rx[0]-rx[2])+ty[2]*(rx[1]-rx[0]))/d;
 candidate.cy=ty[0]-candidate.ay*rx[0]-candidate.by*ry[0];
 if(!calibrationPoint(LCD_X_LENGTH/2,LCD_Y_LENGTH/2,&x,&y))return;
 px=(int)(candidate.ax*x+candidate.bx*y+candidate.cx);py=(int)(candidate.ay*x+candidate.by*y+candidate.cy);
 if(px<(int)LCD_X_LENGTH/2-12||px>(int)LCD_X_LENGTH/2+12||py<(int)LCD_Y_LENGTH/2-12||py>(int)LCD_Y_LENGTH/2+12){calibrated=0;return;}
 cal=candidate;calibrated=1;persisted=saveCalibration();
}
/* DOWN/MOVE/UP/CANCEL = 1/2/3/4. Keep tap anchor stable instead of sampling a sliding release. */
int Touch_Read(unsigned* x,unsigned* y,int* phase){
 static uint32_t last,pressed;static int active,dragged;static unsigned px,py,anchorX,anchorY;
 unsigned rx,ry;int a,b,dx,dy;
 if(!calibrated||(uint32_t)(ui_ms-last)<20)return 0;last=ui_ms;
 if(raw(&rx,&ry)){
  a=(int)(cal.ax*rx+cal.bx*ry+cal.cx);b=(int)(cal.ay*rx+cal.by*ry+cal.cy);
  if(a<0||b<0||a>=LCD_X_LENGTH||b>=LCD_Y_LENGTH){if(active){active=0;*x=px;*y=py;*phase=4;return 1;}return 0;}
  if(!active){active=1;dragged=0;pressed=ui_ms;anchorX=px=(unsigned)a;anchorY=py=(unsigned)b;*phase=1;}
  else{dx=a-(int)anchorX;dy=b-(int)anchorY;if(dx*dx+dy*dy>64)dragged=1;
   if(dragged){px=(px+(unsigned)a)/2;py=(py+(unsigned)b)/2;}*phase=2;
  }
  *x=px;*y=py;return 1;
 }
 if(active&&!down()){active=0;*x=dragged?px:anchorX;*y=dragged?py:anchorY;*phase=(uint32_t)(ui_ms-pressed)>=35?3:4;return 1;}
 return 0;
}
int Touch_Event(unsigned* x,unsigned* y){int phase;return Touch_Read(x,y,&phase)&&phase==3;}
