#ifndef STM_FW_PACKAGE_H
#define STM_FW_PACKAGE_H
#include <stdint.h>
#include <string.h>
#define FW_APP_BASE 0x08008000UL
#define FW_APP_END 0x0807F800UL
#define FW_HEADER_SIZE 64U
#define FW_BOARD_ID 0x10300512UL
static uint32_t fwRead32(const uint8_t* p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t fwCrc(uint32_t c,const uint8_t* p,unsigned n){unsigned k;while(n--){c^=*p++;for(k=0;k<8;k++)c=(c>>1)^((c&1)?0xEDB88320UL:0);}return c;}
static int fwHeaderValid(const uint8_t* p,uint32_t total){uint32_t n=fwRead32(p+8);return !memcmp(p,"STMOTA26",8)&&n>=256&&n<=FW_APP_END-FW_APP_BASE&&total==FW_HEADER_SIZE+n&&fwRead32(p+16)==FW_APP_BASE&&fwRead32(p+20)==FW_BOARD_ID&&fwRead32(p+24)==1;}
static int fwVectorsValid(uint32_t sp,uint32_t reset,uint32_t size){return sp>0x20000000UL&&sp<=0x20010000UL&&!(sp&7)&&(reset&1)&&((reset&~1UL)>=FW_APP_BASE)&&((reset&~1UL)<FW_APP_BASE+size);}
#endif
