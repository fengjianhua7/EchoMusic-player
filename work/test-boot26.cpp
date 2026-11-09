#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <fstream>
#include "../outputs/STM32_OTA_Bootloader/User/fw_package.h"
#define MARKER_PAGE 0x08007800UL
#define FR_OK 0
#define FLASH_COMPLETE 0
#define __disable_irq() ((void)0)
#define __enable_irq() ((void)0)
typedef unsigned UINT;typedef int FLASH_Status;
struct FIL{unsigned pos=0;}image;
static uint8_t header[64],buffer[2048];static std::vector<uint8_t> fileData;
static int failAfter=-1,erases=0,programs=0;
static void report(const char*){}static void FLASH_Unlock(){}static void FLASH_Lock(){}
static int f_size(FIL*){return (int)fileData.size();}static int f_lseek(FIL*f,unsigned n){f->pos=n;return 0;}
static int f_read(FIL*f,void*out,unsigned n,UINT* got){if(f->pos+n>fileData.size())return 1;memcpy(out,fileData.data()+f->pos,n);f->pos+=n;*got=n;return 0;}
static int FLASH_ErasePage(uint32_t a){assert(a==MARKER_PAGE||(a>=FW_APP_BASE&&a+2048<=FW_APP_END));memset((void*)(uintptr_t)a,255,2048);erases++;return 0;}
static int FLASH_ProgramWord(uint32_t a,uint32_t n){assert(a==MARKER_PAGE);*(uint32_t*)(uintptr_t)a=n;return 0;}
static int FLASH_ProgramHalfWord(uint32_t a,uint16_t n){assert(a>=FW_APP_BASE&&a+2<=FW_APP_END);if(failAfter==0)return 1;if(failAfter>0)failAfter--;*(uint16_t*)(uintptr_t)a=n;programs++;return 0;}

static int pending(void){return *(volatile uint32_t*)MARKER_PAGE!=0xffffffffUL;}
static int marker(int set){FLASH_Status r;FLASH_Unlock();__disable_irq();r=FLASH_ErasePage(MARKER_PAGE);if(r==FLASH_COMPLETE&&set)r=FLASH_ProgramWord(MARKER_PAGE,0x55504454UL);__enable_irq();FLASH_Lock();return r==FLASH_COMPLETE&&pending()==set;}
static uint32_t flashCrc(uint32_t size){return fwCrc(0xffffffffUL,(const uint8_t*)FW_APP_BASE,(unsigned)size)^0xffffffffUL;}
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
int main(){void* p=VirtualAlloc((void*)0x08000000,0x80000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(p==(void*)0x08000000);memset(p,255,0x80000);memset((void*)0x0807f800,0x5a,2048);
 std::ifstream in("outputs/Wireless_Update_v26/STM32_v26.stmfw",std::ios::binary);assert(in);fileData.assign(std::istreambuf_iterator<char>(in),{});assert(validate());
 fileData.back()^=1;assert(!validate()&&erases==0);fileData.back()^=1;assert(validate());
 failAfter=20;assert(!install()&&pending());assert(fwVectorsValid(*(uint32_t*)FW_APP_BASE,*(uint32_t*)(FW_APP_BASE+4),fwRead32(header+8))); // vectors alone would be insufficient after interruption
 failAfter=-1;assert(validate()&&install()&&!pending());assert(!memcmp((void*)FW_APP_BASE,fileData.data()+64,fileData.size()-64));for(int i=0;i<2048;i++)assert(((uint8_t*)0x0807f800)[i]==0x5a);
 int count=programs;assert(validate()&&install());assert(programs==count); // identical package avoids needless app erase/program
 for(unsigned a=0x08000000;a<0x08007000;a++)assert(*(uint8_t*)(uintptr_t)a==255);
 puts("Actual bootloader validation/install passed: bad CRC causes zero erase; interrupted write retains marker; retry restores app; boot and calibration regions preserved; matching app skips rewrite");}
