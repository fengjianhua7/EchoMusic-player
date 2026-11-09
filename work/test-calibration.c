#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
static unsigned LCD_X_LENGTH=240,LCD_Y_LENGTH=320,saved,point;static int calibrated,persisted,badCenter;
typedef struct {uint32_t magic,width,height;float ax,bx,cx,ay,by,cy;uint32_t checksum;} Calibration;
static Calibration cal;
static int calibrationPoint(unsigned tx,unsigned ty,unsigned* x,unsigned* y){*x=100+10*ty;*y=4000-12*tx;if(badCenter&&point==3){*x=20;*y=10;}point++;return 1;}
static int saveCalibration(void){saved++;return 1;}
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

int main(void){float x,y;Touch_Calibrate();assert(calibrated&&persisted&&saved==1&&point==4);x=cal.ax*450+cal.bx*3520+cal.cx;y=cal.ay*450+cal.by*3520+cal.cy;assert(fabs(x-40)<0.01&&fabs(y-35)<0.01);
 x=cal.ax*200+cal.bx*1600+cal.cx;y=cal.ay*200+cal.by*1600+cal.cy;assert(fabs(x-200)<0.01&&fabs(y-10)<0.01);
 badCenter=1;point=0;Touch_Calibrate();assert(!calibrated&&saved==1);puts("Calibration tests passed: rotated ADC axes, top tabs, login position, fourth-point rejection before saving");}
