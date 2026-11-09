#include "stm32f10x.h"
#include "ff.h"
#include "sdio/bsp_sdio_sdcard.h"
#include "fw_package.h"
#define MARKER_PAGE 0x08007800UL
const uint32_t bootIdentity __attribute__((at(0x08007000)))=0x26324F54UL;
const uint32_t bootMarkerInitial __attribute__((at(0x08007800)))=0xffffffffUL;
static FATFS fs;
static FIL image;
static FILINFO pendingFile;
static uint8_t header[64],buffer[2048];
static void report(const char* s){while(*s){while(USART_GetFlagStatus(USART3,USART_FLAG_TXE)==RESET){}USART_SendData(USART3,(uint8_t)*s++);}}
static void serialInit(void){GPIO_InitTypeDef g;USART_InitTypeDef u;RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3,ENABLE);GPIO_StructInit(&g);g.GPIO_Pin=GPIO_Pin_10;g.GPIO_Mode=GPIO_Mode_AF_PP;g.GPIO_Speed=GPIO_Speed_50MHz;GPIO_Init(GPIOB,&g);g.GPIO_Pin=GPIO_Pin_0;g.GPIO_Mode=GPIO_Mode_Out_PP;GPIO_Init(GPIOB,&g);GPIO_ResetBits(GPIOB,GPIO_Pin_0);USART_StructInit(&u);u.USART_BaudRate=230400;USART_Init(USART3,&u);USART_Cmd(USART3,ENABLE);}
void SDIO_IRQHandler(void){SD_ProcessIRQSrc();}
static int pending(void){return *(volatile uint32_t*)MARKER_PAGE!=0xffffffffUL;}
static int marker(int set){FLASH_Status r;FLASH_Unlock();__disable_irq();r=FLASH_ErasePage(MARKER_PAGE);if(r==FLASH_COMPLETE&&set)r=FLASH_ProgramWord(MARKER_PAGE,0x55504454UL);__enable_irq();FLASH_Lock();return r==FLASH_COMPLETE&&pending()==set;}
static uint32_t flashCrc(uint32_t size){return fwCrc(0xffffffffUL,(const uint8_t*)FW_APP_BASE,(unsigned)size)^0xffffffffUL;}
/* Full package validation happens before the first application erase. */
static int validate(void){UINT got;uint32_t left,c=0xffffffffUL;unsigned n;
 if(f_size(&image)<FW_HEADER_SIZE||f_lseek(&image,0)!=FR_OK||f_read(&image,header,64,&got)!=FR_OK||got!=64||!fwHeaderValid(header,f_size(&image)))return 0;
 left=fwRead32(header+8);
 while(left){n=left>sizeof(buffer)?sizeof(buffer):(unsigned)left;if(f_read(&image,buffer,n,&got)!=FR_OK||got!=n)return 0;
  if(left==fwRead32(header+8)&&!fwVectorsValid(fwRead32(buffer),fwRead32(buffer+4),fwRead32(header+8)))return 0;
  c=fwCrc(c,buffer,n);left-=n;
 }
 return (c^0xffffffffUL)==fwRead32(header+12);
}
static int install(void){uint32_t size=fwRead32(header+8),address,left;UINT got;unsigned n,i;FLASH_Status status=FLASH_COMPLETE;
 if(flashCrc(size)==fwRead32(header+12)&&fwVectorsValid(*(uint32_t*)FW_APP_BASE,*(uint32_t*)(FW_APP_BASE+4),size))return marker(0);
 if(!marker(1))return 0;report("BOOT:WRITING\n");FLASH_Unlock();
 for(address=FW_APP_BASE;address<FW_APP_BASE+size;address+=2048){__disable_irq();status=FLASH_ErasePage(address);__enable_irq();if(status!=FLASH_COMPLETE)goto failure;}
 if(f_lseek(&image,64)!=FR_OK)goto failure;
 left=size;address=FW_APP_BASE;
 while(left){n=left>sizeof(buffer)?sizeof(buffer):(unsigned)left;if(f_read(&image,buffer,n,&got)!=FR_OK||got!=n)goto failure;
  for(i=0;i<n;i+=2){uint16_t word=buffer[i]|((uint16_t)(i+1<n?buffer[i+1]:255)<<8);__disable_irq();status=FLASH_ProgramHalfWord(address+i,word);__enable_irq();if(status!=FLASH_COMPLETE)goto failure;}
  address+=n;left-=n;
 }
 FLASH_Lock();if(flashCrc(size)!=fwRead32(header+12))return 0;return marker(0);
failure:FLASH_Lock();return 0;
}
__asm void branchApp(uint32_t sp,uint32_t entry)
{
    MSR MSP, r0
    BX r1
}
static void jump(void){unsigned i;uint32_t sp=*(uint32_t*)FW_APP_BASE,entry=*(uint32_t*)(FW_APP_BASE+4);
 __disable_irq();SysTick->CTRL=0;SysTick->LOAD=0;SysTick->VAL=0;
 for(i=0;i<8;i++){NVIC->ICER[i]=0xffffffffUL;NVIC->ICPR[i]=0xffffffffUL;}
 SDIO->MASK=0;SDIO->DCTRL=0;SDIO->POWER=0;DMA2_Channel4->CCR=0;USART_Cmd(USART3,DISABLE);RCC_DeInit();SCB->VTOR=FW_APP_BASE;__DSB();__ISB();__enable_irq();branchApp(sp,entry);
}
int main(void){volatile unsigned delay;int ok;serialInit();report("BOOT:26\n");
 for(;;){ok=0;
  if(f_mount(&fs,"0:",1)==FR_OK&&f_stat("0:/FWUPD.TMP",&pendingFile)==FR_NO_FILE&&f_open(&image,"0:/STMUPD.BIN",FA_READ)==FR_OK){
   if(validate()){ok=install();f_close(&image);if(ok){f_unlink("0:/STMUPD.BIN");report("BOOT:VERIFIED\n");}}
   else {f_close(&image);report("BOOT:PACKAGE_INVALID\n");}
  }
  if(!pending()&&fwVectorsValid(*(uint32_t*)FW_APP_BASE,*(uint32_t*)(FW_APP_BASE+4),FW_APP_END-FW_APP_BASE))jump();
  report("BOOT:RECOVERY_KEEP_TF\n");for(delay=0;delay<3000000;delay++){}
 }
}
