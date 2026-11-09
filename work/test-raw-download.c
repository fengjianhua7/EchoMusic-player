#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef unsigned UINT;typedef int FRESULT;typedef struct{int unused;}FIL;typedef struct{int unused;}FILINFO;
#define FR_OK 0
#define FR_NO_FILE 4
#define FA_WRITE 1
#define FA_CREATE_NEW 2
static FIL media;static int mode,failed,rawDownload;static uint32_t bytes,crc,rawDownloadSize;
static char temporary[24],target[24],reply[128],renamed[24];static uint8_t disk[4096];static unsigned diskSize,at;static int failWrite;
void Player_Reply(const char* s){strcpy(reply,s);}static int mountCard(void){return 1;}static void stop(void){mode=0;}
static int f_stat(const char* s,FILINFO* i){return FR_NO_FILE;}
static int f_open(FIL* f,const char* p,unsigned flags){diskSize=at=0;return FR_OK;}
static int f_write(FIL* f,const void* data,unsigned n,UINT* out){if(failWrite){*out=0;return 1;}assert(at+n<=sizeof(disk));memcpy(disk+at,data,n);at+=n;if(at>diskSize)diskSize=at;*out=n;return 0;}
static int f_lseek(FIL*f,unsigned n){at=n;return 0;}static int f_sync(FIL*f){return 0;}static int f_close(FIL*f){return 0;}
static int f_rename(const char*a,const char*b){strcpy(renamed,b);return 0;}

static void put32(uint8_t* b,uint32_t n){b[0]=n;b[1]=n>>8;b[2]=n>>16;b[3]=n>>24;}
static uint32_t crcAdd(uint32_t c,const uint8_t* b,unsigned n){
 static const uint32_t table[16]={0x00000000UL,0x1DB71064UL,0x3B6E20C8UL,0x26D930ACUL,0x76DC4190UL,0x6B6B51F4UL,0x4DB26158UL,0x5005713CUL,0xEDB88320UL,0xF00F9344UL,0xD6D6A3E8UL,0xCB61B38CUL,0x9B64C2B0UL,0x86D3D2D4UL,0xA00AE278UL,0xBDBDF21CUL};
 while(n--){c^=*b++;c=(c>>4)^table[c&15];c=(c>>4)^table[c&15];}return c;
}
void Storage_Consume(const uint8_t* data){UINT n=0;unsigned take=512;
 if(mode!=1||failed)return;
 if(rawDownload){if(bytes>=rawDownloadSize){failed=1;Player_Reply("SD_ERR:EXCESS_DATA\n");return;}if(rawDownloadSize-bytes<take)take=(unsigned)(rawDownloadSize-bytes);}
 if(f_write(&media,data,take,&n)!=FR_OK||n!=take){failed=1;Player_Reply("SD_ERR:WRITE\n");return;}
 bytes+=take;crc=crcAdd(crc,data,take);
}
static void startDownload(int raw,uint32_t size){unsigned id;FILINFO info;UINT n;uint8_t h[44]={0};
 stop();rawDownload=raw;rawDownloadSize=size;if(raw&&!size){Player_Reply("SD_ERR:SIZE\n");return;}if(!mountCard())return;memset(&info,0,sizeof(info));
 for(id=1;id<100000;id++){snprintf(target,sizeof(target),raw?"0:/M%05u.MP3":"0:/M%05u.WAV",id);snprintf(temporary,sizeof(temporary),"0:/M%05u.TMP",id);if(f_stat(target,&info)==FR_NO_FILE&&f_stat(temporary,&info)==FR_NO_FILE)break;}
 if(id==100000||f_open(&media,temporary,FA_WRITE|FA_CREATE_NEW)!=FR_OK){Player_Reply("SD_ERR:CREATE\n");return;}
 mode=1;failed=0;bytes=0;crc=0xffffffffUL;
 if(!raw&&(f_write(&media,h,44,&n)!=FR_OK||n!=44)){stop();Player_Reply("SD_ERR:HEADER\n");return;}
 Player_Reply("SD_READY\n");
}
static void finishDownload(const char* line){unsigned long expected,checksum;UINT n;uint8_t h[44]={0};char response[48];
 if(mode!=1){Player_Reply("SD_ERR:NO_DOWNLOAD\n");return;}
 if(sscanf(line,"%lu:%lx",&expected,&checksum)!=2||failed||expected!=bytes||(rawDownload&&bytes!=rawDownloadSize)||checksum!=(crc^0xffffffffUL)){stop();Player_Reply("SD_ERR:VERIFY\n");return;}
 memcpy(h,"RIFF",4);put32(h+4,bytes+36);memcpy(h+8,"WAVEfmt ",8);put32(h+16,16);h[20]=1;h[22]=1;put32(h+24,32000);put32(h+28,64000);h[32]=2;h[34]=16;memcpy(h+36,"data",4);put32(h+40,bytes);
 if((!rawDownload&&(f_lseek(&media,0)!=FR_OK||f_write(&media,h,44,&n)!=FR_OK||n!=44))||f_sync(&media)!=FR_OK){stop();Player_Reply("SD_ERR:FINALIZE\n");return;}
 if(f_close(&media)!=FR_OK){mode=0;Player_Reply("SD_ERR:CLOSE\n");return;}mode=0;
 if(f_rename(temporary,target)!=FR_OK){Player_Reply("SD_ERR:RENAME\n");return;}
 snprintf(response,sizeof(response),"SD_DONE:%s\n",target+3);Player_Reply(response);
}
int main(void){uint8_t data[1024];char ending[64];unsigned i;for(i=0;i<1024;i++)data[i]=(uint8_t)(i*7);
 startDownload(1,513);assert(mode==1&&!strcmp(reply,"SD_READY\n")&&diskSize==0);
 Storage_Consume(data);Storage_Consume(data+512);assert(bytes==513&&diskSize==513&&!memcmp(disk,data,513));
 snprintf(ending,sizeof(ending),"513:%lx",(unsigned long)(crcAdd(0xffffffffUL,data,513)^0xffffffffUL));finishDownload(ending);
 assert(!strcmp(renamed,"0:/M00001.MP3")&&mode==0&&diskSize==513);
 renamed[0]=0;startDownload(1,513);Storage_Consume(data);finishDownload("512:00000000");assert(!renamed[0]&&!strcmp(reply,"SD_ERR:VERIFY\n"));
 startDownload(1,513);failWrite=1;Storage_Consume(data);failWrite=0;assert(failed&&!strcmp(reply,"SD_ERR:WRITE\n"));finishDownload("513:00000000");assert(!renamed[0]);
 startDownload(0,0);Storage_Consume(data);snprintf(ending,sizeof(ending),"512:%lx",(unsigned long)(crcAdd(0xffffffffUL,data,512)^0xffffffffUL));finishDownload(ending);assert(diskSize==556&&!memcmp(disk,"RIFF",4)&&!strcmp(renamed,"0:/M00001.WAV"));
 puts("Actual STM32 download functions passed: 513-byte MP3 exact payload/CRC, failed/incomplete unpublished, legacy WAV header retained");}
