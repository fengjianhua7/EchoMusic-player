#include "stm32f10x.h"
#include "lcd/bsp_ili9341_lcd.h"
extern volatile uint32_t ui_ms;
static float ax=-0.001848f,bx=0.066984f,cx=-12.807136f;
static float ay=-0.084858f,by=-0.000805f,cy=333.395386f;
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
}
/* Three-point calibration, active until STM32 is reset. */
void Touch_Calibrate(void) {
 float rx[3],ry[3],tx[3],ty[3],d;
 unsigned i,x,y;uint32_t start;
 tx[0]=20;ty[0]=35;tx[1]=LCD_X_LENGTH-20;ty[1]=35;
 tx[2]=LCD_X_LENGTH/2;ty[2]=LCD_Y_LENGTH-25;
 for(i=0;i<3;i++) {
  LCD_SetColors(BLACK,WHITE);ILI9341_Clear(0,0,LCD_X_LENGTH,LCD_Y_LENGTH);
  ILI9341_DispString_EN(4,4,"Tap cross, then release");
  ILI9341_DrawLine((uint16_t)tx[i]-8,(uint16_t)ty[i],(uint16_t)tx[i]+8,(uint16_t)ty[i]);
  ILI9341_DrawLine((uint16_t)tx[i],(uint16_t)ty[i]-8,(uint16_t)tx[i],(uint16_t)ty[i]+8);
  while(down()){}
  start=ui_ms;
  do {if((uint32_t)(ui_ms-start)>30000)return;}while(!raw(&x,&y));
  rx[i]=(float)x;ry[i]=(float)y;
  while(down()){}
 }
 d=rx[0]*(ry[1]-ry[2])+rx[1]*(ry[2]-ry[0])+rx[2]*(ry[0]-ry[1]);
 if(d<100 && d>-100)return;
 ax=(tx[0]*(ry[1]-ry[2])+tx[1]*(ry[2]-ry[0])+tx[2]*(ry[0]-ry[1]))/d;
 bx=(tx[0]*(rx[2]-rx[1])+tx[1]*(rx[0]-rx[2])+tx[2]*(rx[1]-rx[0]))/d;
 cx=tx[0]-ax*rx[0]-bx*ry[0];
 ay=(ty[0]*(ry[1]-ry[2])+ty[1]*(ry[2]-ry[0])+ty[2]*(ry[0]-ry[1]))/d;
 by=(ty[0]*(rx[2]-rx[1])+ty[1]*(rx[0]-rx[2])+ty[2]*(rx[1]-rx[0]))/d;
 cy=ty[0]-ay*rx[0]-by*ry[0];
}
int Touch_Event(unsigned* x,unsigned* y) {
 static uint32_t last,pressed;
 static int active;
 static unsigned px,py;
 unsigned rx,ry;int a,b;
 if((uint32_t)(ui_ms-last)<20)return 0;last=ui_ms;
 if(raw(&rx,&ry)) {
  a=(int)(ax*rx+bx*ry+cx);b=(int)(ay*rx+by*ry+cy);
  if(a<0||b<0||a>=LCD_X_LENGTH||b>=LCD_Y_LENGTH)return 0;
  px=(unsigned)a;py=(unsigned)b;
  if(!active){active=1;pressed=ui_ms;}
 } else if(active && !down()) {
  active=0;
  if((uint32_t)(ui_ms-pressed)>=35){*x=px;*y=py;return 1;}
 }
 return 0;
}

