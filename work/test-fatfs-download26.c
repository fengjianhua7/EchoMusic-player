#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "ff.h"
#include "diskio.h"
static FATFS fs;static FIL media,downloadMarker;static int mode,failed,rawDownload;static uint32_t bytes,crc,rawDownloadSize;static unsigned downloadBuffered;static uint32_t mp3ReadAhead[2048];static char temporary[24],target[24],reply[1024],allReplies[16000];static char indexedNames[128][13];static unsigned indexedCount;static int mounted;
static uint8_t disk[32768*512];static unsigned writeCalls;
DSTATUS disk_initialize(BYTE d){return 0;}DSTATUS disk_status(BYTE d){return 0;}
DRESULT disk_read(BYTE d,BYTE*out,DWORD sector,UINT n){if(sector+n>32768)return RES_PARERR;memcpy(out,disk+sector*512,n*512);return RES_OK;}
DRESULT disk_write(BYTE d,const BYTE*in,DWORD sector,UINT n){if(sector+n>32768)return RES_PARERR;memcpy(disk+sector*512,in,n*512);writeCalls++;return RES_OK;}
DRESULT disk_ioctl(BYTE d,BYTE c,void*out){if(c==GET_SECTOR_COUNT)*(DWORD*)out=32768;if(c==GET_SECTOR_SIZE)*(WORD*)out=512;if(c==GET_BLOCK_SIZE)*(DWORD*)out=1;return RES_OK;}
DWORD get_fattime(void){return (46UL<<25)|(11UL<<21)|(3UL<<16);}
void Player_Reply(const char*s){strcpy(reply,s);if(strlen(allReplies)+strlen(s)<sizeof(allReplies))strcat(allReplies,s);}
static int mountCard(void){return 1;}static void stop(void){if(mode)f_close(&media);mode=0;}

static void put32(uint8_t* b,uint32_t n){b[0]=n;b[1]=n>>8;b[2]=n>>16;b[3]=n>>24;}
static uint32_t crcAdd(uint32_t c,const uint8_t* b,unsigned n){
 static const uint32_t table[16]={0x00000000UL,0x1DB71064UL,0x3B6E20C8UL,0x26D930ACUL,0x76DC4190UL,0x6B6B51F4UL,0x4DB26158UL,0x5005713CUL,0xEDB88320UL,0xF00F9344UL,0xD6D6A3E8UL,0xCB61B38CUL,0x9B64C2B0UL,0x86D3D2D4UL,0xA00AE278UL,0xBDBDF21CUL};
 while(n--){c^=*b++;c=(c>>4)^table[c&15];c=(c>>4)^table[c&15];}return c;
}
static int flushDownload(void){UINT n=0;if(!downloadBuffered)return 1;if(f_write(&media,mp3ReadAhead,downloadBuffered,&n)!=FR_OK||n!=downloadBuffered){failed=1;Player_Reply("SD_ERR:WRITE\n");return 0;}downloadBuffered=0;return 1;}
void Storage_Consume(const uint8_t* data){unsigned take=512,at=0,n;
 if(mode!=1||failed)return;
 if(rawDownload){if(bytes>=rawDownloadSize){failed=1;Player_Reply("SD_ERR:EXCESS_DATA\n");return;}if(rawDownloadSize-bytes<take)take=(unsigned)(rawDownloadSize-bytes);}
 while(at<take){n=sizeof(mp3ReadAhead)-downloadBuffered;if(n>take-at)n=take-at;memcpy(((uint8_t*)mp3ReadAhead)+downloadBuffered,data+at,n);downloadBuffered+=n;at+=n;if(downloadBuffered==sizeof(mp3ReadAhead)&&!flushDownload())return;}
 bytes+=take;crc=crcAdd(crc,data,take);
}
static void startDownload(int raw,uint32_t size){unsigned id;FILINFO info;UINT n;uint8_t h[44]={0};
 stop();rawDownload=raw;rawDownloadSize=size;if(raw&&!size){Player_Reply("SD_ERR:SIZE\n");return;}if(!mountCard())return;memset(&info,0,sizeof(info));
 if(raw==2){strcpy(target,"0:/STMUPD.BIN");strcpy(temporary,"0:/FWUPD.TMP");f_unlink(temporary);id=1;}
 else for(id=1;id<100000;id++){snprintf(target,sizeof(target),raw?"0:/M%05u.MP3":"0:/M%05u.WAV",id);snprintf(temporary,sizeof(temporary),"0:/M%05u.TMP",id);if(f_stat(target,&info)==FR_NO_FILE&&f_stat(temporary,&info)==FR_NO_FILE)break;}
 if(id==100000||f_open(&downloadMarker,temporary,FA_WRITE|FA_CREATE_NEW)!=FR_OK){Player_Reply("SD_ERR:CREATE\n");return;}
 if(f_close(&downloadMarker)!=FR_OK||f_open(&media,target,FA_WRITE|(raw==2?FA_CREATE_ALWAYS:FA_CREATE_NEW))!=FR_OK){Player_Reply("SD_ERR:CREATE_DATA\n");return;}
 mode=1;failed=0;bytes=0;crc=0xffffffffUL;downloadBuffered=0;
 if(!raw&&(f_write(&media,h,44,&n)!=FR_OK||n!=44)){stop();Player_Reply("SD_ERR:HEADER\n");return;}
 Player_Reply("SD_READY\n");
}
static void finishDownload(const char* line){unsigned long expected,checksum;UINT n;uint8_t h[44]={0};char response[48];
 if(mode!=1){Player_Reply("SD_ERR:NO_DOWNLOAD\n");return;}
 if(sscanf(line,"%lu:%lx",&expected,&checksum)!=2||failed||expected!=bytes||(rawDownload&&bytes!=rawDownloadSize)||checksum!=(crc^0xffffffffUL)){stop();Player_Reply("SD_ERR:VERIFY\n");return;}
 if(!flushDownload()){stop();return;}
 memcpy(h,"RIFF",4);put32(h+4,bytes+36);memcpy(h+8,"WAVEfmt ",8);put32(h+16,16);h[20]=1;h[22]=1;put32(h+24,32000);put32(h+28,64000);h[32]=2;h[34]=16;memcpy(h+36,"data",4);put32(h+40,bytes);
 if((!rawDownload&&(f_lseek(&media,0)!=FR_OK||f_write(&media,h,44,&n)!=FR_OK||n!=44))||f_sync(&media)!=FR_OK){stop();Player_Reply("SD_ERR:FINALIZE\n");return;}
 if(f_close(&media)!=FR_OK){mode=0;Player_Reply("SD_ERR:CLOSE\n");return;}mode=0;
 /* Publish by removing the incomplete marker; payload never needs renaming. */
 {FRESULT result=f_unlink(temporary);if(result!=FR_OK){snprintf(response,sizeof(response),"SD_ERR:PUBLISH:%u:%s\n",(unsigned)result,temporary+3);Player_Reply(response);return;}}
 snprintf(response,sizeof(response),"SD_DONE:%s\n",target+3);Player_Reply(response);
}
static void utf8Name(const char* source,char* dest,unsigned capacity) {
 unsigned used=0;uint16_t c;const uint8_t* p=(const uint8_t*)source;
 while(*p && used+4<capacity) {
  c=*p++;
  if(c>=128 && *p)c=ff_convert((WCHAR)((c<<8)|*p++),1);
  if(!c)c='?';
  if(c<128)dest[used++]=(char)c;
  else if(c<2048){dest[used++]=(char)(0xc0|(c>>6));dest[used++]=(char)(0x80|(c&63));}
  else {dest[used++]=(char)(0xe0|(c>>12));dest[used++]=(char)(0x80|((c>>6)&63));dest[used++]=(char)(0x80|(c&63));}
 }
 dest[used]=0;
}
static void listFiles(void){
 DIR dir;FILINFO info;size_t len;FRESULT result;static char longName[256],display[768],response[900];
 if(mode){Player_Reply("SD_ERR:BUSY\n");return;}if(!mountCard())return;
 memset(&info,0,sizeof(info));info.lfname=longName;info.lfsize=sizeof(longName);
 result=f_opendir(&dir,"0:/");
 if(result!=FR_OK){mounted=0;snprintf(response,sizeof(response),"SD_ERR:LIST:%u\n",(unsigned)result);Player_Reply(response);return;}
 indexedCount=0;Player_Reply("SD_LIST_BEGIN\n");
 while(indexedCount<128){
  longName[0]=0;result=f_readdir(&dir,&info);
  if(result!=FR_OK){f_closedir(&dir);mounted=0;snprintf(response,sizeof(response),"SD_ERR:READDIR:%u\n",(unsigned)result);Player_Reply(response);return;}
  if(!info.fname[0])break;
  len=strlen(info.fname);if(info.fattrib&AM_DIR||len<4)continue;
  if(strcmp(info.fname+len-4,".WAV")&&strcmp(info.fname+len-4,".wav")&&strcmp(info.fname+len-4,".MP3")&&strcmp(info.fname+len-4,".mp3"))continue;
  if(len==10&&info.fname[0]=='M'&&info.fname[6]=='.'){char markerName[24];FILINFO pending;snprintf(markerName,sizeof(markerName),"0:/%.6s.TMP",info.fname);memset(&pending,0,sizeof(pending));if(f_stat(markerName,&pending)==FR_OK)continue;}
  strncpy(indexedNames[indexedCount],info.fname,12);indexedNames[indexedCount][12]=0;
  utf8Name(longName[0]?longName:info.fname,display,sizeof(display));
  snprintf(response,sizeof(response),"SD_FILE:%s:%lu\n",display,(unsigned long)info.fsize);Player_Reply(response);indexedCount++;
 }
 f_closedir(&dir);Player_Reply("SD_LIST_END\n");
}
static void put16(uint8_t*p,unsigned v){p[0]=v;p[1]=v>>8;}
int main(void){unsigned i;uint8_t data[9216],gotData[9001];UINT got;FIL read;FILINFO info;char end[64],first[24],second[24];
 memset(disk,0,sizeof(disk));disk[0]=0xeb;disk[1]=0x3c;disk[2]=0x90;memcpy(disk+3,"MSDOS5.0",8);put16(disk+11,512);disk[13]=4;put16(disk+14,1);disk[16]=2;put16(disk+17,512);put16(disk+19,32768);disk[21]=0xf8;put16(disk+22,32);memcpy(disk+54,"FAT16   ",8);disk[510]=0x55;disk[511]=0xaa;put16(disk+512,0xfff8);put16(disk+514,0xffff);memcpy(disk+33*512,disk+512,32*512);
 assert(f_mount(&fs,"0:",1)==FR_OK);for(i=0;i<sizeof(data);i++)data[i]=(uint8_t)(i*13);
 startDownload(1,9001);assert(!strcmp(reply,"SD_READY\n"));strcpy(first,target);assert(f_stat(temporary,&info)==FR_OK);
 for(i=0;i<9001;i+=512)Storage_Consume(data+i);assert(bytes==9001&&downloadBuffered==809);
 snprintf(end,sizeof(end),"9001:%lx",(unsigned long)(crcAdd(0xffffffffUL,data,9001)^0xffffffffUL));finishDownload(end);assert(!strncmp(reply,"SD_DONE:",8));assert(f_stat(temporary,&info)==FR_NO_FILE);
 assert(f_open(&read,first,FA_READ)==FR_OK&&f_size(&read)==9001);assert(f_read(&read,gotData,9001,&got)==FR_OK&&got==9001&&!memcmp(data,gotData,9001));f_close(&read);
 startDownload(1,9001);strcpy(second,target);Storage_Consume(data);finishDownload("512:00000000");assert(!strcmp(reply,"SD_ERR:VERIFY\n"));assert(f_stat(temporary,&info)==FR_OK);
 allReplies[0]=0;listFiles();assert(indexedCount==1&&strstr(allReplies,"M00001.MP3")&&!strstr(allReplies,"M00002.MP3"));
 startDownload(2,513);Storage_Consume(data);Storage_Consume(data+512);snprintf(end,sizeof(end),"513:%lx",(unsigned long)(crcAdd(0xffffffffUL,data,513)^0xffffffffUL));finishDownload(end);assert(!strcmp(reply,"SD_DONE:STMUPD.BIN\n")&&f_stat("0:/FWUPD.TMP",&info)==FR_NO_FILE);assert(f_stat("0:/STMUPD.BIN",&info)==FR_OK&&info.fsize==513);
 printf("Actual FatFs + actual download functions passed: 8KB buffering, exact 9001-byte file/CRC, marker commit without rename, incomplete file hidden, STM package staged; disk writes=%u\n",writeCalls);
}
