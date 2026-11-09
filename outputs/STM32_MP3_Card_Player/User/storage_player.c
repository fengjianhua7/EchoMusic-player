#include "stm32f10x.h"
#include "ff.h"
#include "sdio/bsp_sdio_sdcard.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
extern void Player_Reply(const char* text);
extern unsigned Audio_Free(void);
extern void Audio_Push(const uint8_t* pcm,unsigned count,unsigned volume);
extern void Audio_Clear(void);
extern unsigned Audio_Queued(void);
extern uint32_t Audio_Underruns(void);
extern void Audio_ResetStats(void);
static FATFS fs;
static FIL media;
static int mounted,mode,failed,paused;
static unsigned volume=25;
static uint32_t bytes,crc,left;
static char temporary[24],target[24];
static char indexedNames[20][13];
static unsigned indexedCount;
static uint8_t block[512];
static uint32_t mp3ReadAhead[2048];
static unsigned mp3CacheAt,mp3CacheSize;
static int fileTransfer;
extern void Audio_PrepareReturn(const uint8_t* data);
static uint32_t storage_le32(const uint8_t* b){return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static void put32(uint8_t* b,uint32_t n){b[0]=n;b[1]=n>>8;b[2]=n>>16;b[3]=n>>24;}
/* Reflected CRC-32, same polynomial and initial/final values as before. */
static uint32_t crcAdd(uint32_t c,const uint8_t* b,unsigned n){
 static const uint32_t table[16]={0x00000000UL,0x1DB71064UL,0x3B6E20C8UL,0x26D930ACUL,0x76DC4190UL,0x6B6B51F4UL,0x4DB26158UL,0x5005713CUL,0xEDB88320UL,0xF00F9344UL,0xD6D6A3E8UL,0xCB61B38CUL,0x9B64C2B0UL,0x86D3D2D4UL,0xA00AE278UL,0xBDBDF21CUL};
 while(n--){c^=*b++;c=(c>>4)^table[c&15];c=(c>>4)^table[c&15];}return c;
}
static int mountCard(void){FRESULT r;if(mounted)return 1;r=f_mount(&fs,"0:",1);if(r!=FR_OK){Player_Reply("SD_ERR:MOUNT\n");return 0;}mounted=1;return 1;}
static void stop(void){if(mode)f_close(&media);mode=0;paused=0;fileTransfer=0;Audio_Clear();}
int Storage_Downloading(void){return mode==1;}
int Storage_Local(void){return mode==2;}
int Storage_TakeReadTransfer(void){int flag=fileTransfer;fileTransfer=0;return flag;}
void Storage_Consume(const uint8_t* data){UINT n=0;if(mode!=1||failed)return;if(f_write(&media,data,512,&n)!=FR_OK||n!=512){failed=1;Player_Reply("SD_ERR:WRITE\n");return;}bytes+=512;crc=crcAdd(crc,data,512);}
static int validName(const char* n){unsigned i,len=(unsigned)strlen(n);if(!len||len>12||n[0]=='.')return 0;for(i=0;i<len;i++)if(!((n[i]>='A'&&n[i]<='Z')||(n[i]>='a'&&n[i]<='z')||(n[i]>='0'&&n[i]<='9')||n[i]=='_'||n[i]=='.'||n[i]=='~'))return 0;return 1;}
static void startDownload(void){unsigned id;FILINFO info;UINT n;uint8_t h[44]={0};
 stop();if(!mountCard())return;memset(&info,0,sizeof(info));
 for(id=1;id<100000;id++){snprintf(target,sizeof(target),"0:/M%05u.WAV",id);snprintf(temporary,sizeof(temporary),"0:/M%05u.TMP",id);if(f_stat(target,&info)==FR_NO_FILE&&f_stat(temporary,&info)==FR_NO_FILE)break;}
 if(id==100000||f_open(&media,temporary,FA_WRITE|FA_CREATE_NEW)!=FR_OK){Player_Reply("SD_ERR:CREATE\n");return;}
 mode=1;failed=0;bytes=0;crc=0xffffffffUL;
 if(f_write(&media,h,44,&n)!=FR_OK||n!=44){stop();Player_Reply("SD_ERR:HEADER\n");return;}
 Player_Reply("SD_READY\n");
}
static void finishDownload(const char* line){unsigned long expected,checksum;UINT n;uint8_t h[44]={0};char response[48];
 if(mode!=1){Player_Reply("SD_ERR:NO_DOWNLOAD\n");return;}
 if(sscanf(line,"%lu:%lx",&expected,&checksum)!=2||failed||expected!=bytes||checksum!=(crc^0xffffffffUL)){stop();Player_Reply("SD_ERR:VERIFY\n");return;}
 memcpy(h,"RIFF",4);put32(h+4,bytes+36);memcpy(h+8,"WAVEfmt ",8);put32(h+16,16);h[20]=1;h[22]=1;put32(h+24,32000);put32(h+28,64000);h[32]=2;h[34]=16;memcpy(h+36,"data",4);put32(h+40,bytes);
 if(f_lseek(&media,0)!=FR_OK||f_write(&media,h,44,&n)!=FR_OK||n!=44||f_sync(&media)!=FR_OK){stop();Player_Reply("SD_ERR:FINALIZE\n");return;}
 if(f_close(&media)!=FR_OK){mode=0;Player_Reply("SD_ERR:CLOSE\n");return;}mode=0;
 if(f_rename(temporary,target)!=FR_OK){Player_Reply("SD_ERR:RENAME\n");return;}
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
 DIR dir;FILINFO info;size_t len;static char longName[256],display[768],response[900];
 if(mode){Player_Reply("SD_ERR:BUSY\n");return;}if(!mountCard())return;
 indexedCount=0;memset(&info,0,sizeof(info));info.lfname=longName;info.lfsize=sizeof(longName);
 Player_Reply("SD_LIST_BEGIN\n");
 if(f_opendir(&dir,"0:/")!=FR_OK){Player_Reply("SD_ERR:LIST\n");return;}
 while(indexedCount<20 && f_readdir(&dir,&info)==FR_OK && info.fname[0]){
  len=strlen(info.fname);if(info.fattrib&AM_DIR||len<4)continue;
  if(strcmp(info.fname+len-4,".WAV")&&strcmp(info.fname+len-4,".wav")&&strcmp(info.fname+len-4,".MP3")&&strcmp(info.fname+len-4,".mp3"))continue;
  strncpy(indexedNames[indexedCount],info.fname,12);indexedNames[indexedCount][12]=0;
  utf8Name(longName[0]?longName:info.fname,display,sizeof(display));
  snprintf(response,sizeof(response),"SD_FILE:%s:%lu\n",display,(unsigned long)info.fsize);Player_Reply(response);indexedCount++;
 }
 f_closedir(&dir);Player_Reply("SD_LIST_END\n");
}
static void openLocal(const char* name,int indexed){uint8_t h[16];UINT got;DWORD size;int format=0;char path[24];
 stop();if(!indexed && !validName(name)){Player_Reply("SD_ERR:NAME\n");return;}if(!mountCard())return;
 snprintf(path,sizeof(path),"0:/%s",name);if(f_open(&media,path,FA_READ)!=FR_OK){Player_Reply("SD_ERR:OPEN\n");return;}
 if(strlen(name)>=4 && (!strcmp(name+strlen(name)-4,".MP3")||!strcmp(name+strlen(name)-4,".mp3"))) {
  char response[48];mode=3;left=f_size(&media);mp3CacheAt=mp3CacheSize=0;
  snprintf(response,sizeof(response),"SD_MP3:%lu\n",(unsigned long)left);Player_Reply(response);return;
 }
 mode=2;
 if(f_read(&media,h,12,&got)!=FR_OK||got!=12||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4))goto invalid;
 while(f_tell(&media)+8<=f_size(&media)) {
  if(f_read(&media,h,8,&got)!=FR_OK||got!=8)goto invalid;
  size=storage_le32(h+4);if(size>f_size(&media)-f_tell(&media))goto invalid;
  if(!memcmp(h,"fmt ",4)) {
   if(size<16||f_read(&media,h,16,&got)!=FR_OK||got!=16)goto invalid;
   format=h[0]==1&&h[1]==0&&h[2]==1&&h[3]==0&&storage_le32(h+4)==32000&&h[12]==2&&h[14]==16;
   if(!format||f_lseek(&media,f_tell(&media)+size-16+(size&1))!=FR_OK)goto invalid;
  } else if(!memcmp(h,"data",4)) {if(!format||(size&1))goto invalid;left=size;Player_Reply("SD_PLAYING\n");return;}
  else if(f_lseek(&media,f_tell(&media)+size+(size&1))!=FR_OK)goto invalid;
 }
invalid:stop();Player_Reply("SD_ERR:WAV_FORMAT\n");
}
void Storage_Poll(void){UINT got;unsigned n;if(mode!=2||paused||Audio_Free()<256)return;
 if(!left){if(Audio_Queued())return;f_close(&media);mode=0;Player_Reply("SD_FINISHED\n");return;}
 n=left>512?512:(unsigned)left;
 if(f_read(&media,block,n,&got)!=FR_OK||got!=n){stop();Player_Reply("SD_ERR:READ\n");return;}
 memset(block+n,0,512-n);left-=n;Audio_Push(block,256,volume);
}
static void readMp3Block(void) {
 UINT got;unsigned n;char response[64];
 if(mode!=3||!left){Player_Reply("SD_ERR:NO_MP3_DATA\n");return;}
 n=left>512?512:(unsigned)left;
 if(mp3CacheAt==mp3CacheSize) {
  unsigned request=left>sizeof(mp3ReadAhead)?sizeof(mp3ReadAhead):(unsigned)left;
  if(f_read(&media,mp3ReadAhead,request,&got)!=FR_OK||got!=request){stop();Player_Reply("SD_ERR:READ\n");return;}
  mp3CacheAt=0;mp3CacheSize=got;
 }
 memcpy(block,((const uint8_t*)mp3ReadAhead)+mp3CacheAt,n);mp3CacheAt+=n;
 memset(block+n,0,512-n);left-=n;fileTransfer=1;
 Audio_PrepareReturn(block);
 snprintf(response,sizeof(response),"SD_BLOCK:%u:%08lX\n",n,(unsigned long)(crcAdd(0xffffffffUL,block,512)^0xffffffffUL));
 Player_Reply(response);
}
int Storage_Command(const char* line){
 if(!strcmp(line,"Z")){Audio_ResetStats();Player_Reply("SD_STATS_RESET\n");return 1;}
 if(!strcmp(line,"U")){char report[48];snprintf(report,sizeof(report),"SD_UNDERRUN:%lu\n",(unsigned long)Audio_Underruns());Player_Reply(report);return 1;}
 if(!strncmp(line,"I:",2)){int index=atoi(line+2);if(index<0||(unsigned)index>=indexedCount)Player_Reply("SD_ERR:INDEX\n");else openLocal(indexedNames[index],1);return 1;}
 if(!strcmp(line,"B")){readMp3Block();return 1;}
 if(!strcmp(line,"D")){startDownload();return 1;}
 if(!strncmp(line,"E:",2)){finishDownload(line+2);return 1;}
 if(!strcmp(line,"V")){listFiles();return 1;}
 if(!strncmp(line,"O:",2)){openLocal(line+2,0);return 1;}
 if(!strcmp(line,"X")){stop();Player_Reply("SD_STOPPED\n");return 1;}
 if(!strcmp(line,"H")){if(mode==2){paused=!paused;Player_Reply(paused?"SD_PAUSED\n":"SD_PLAYING\n");}return 1;}
 if(!strncmp(line,"G:",2)){int v=atoi(line+2);if(v>=0&&v<=100)volume=(unsigned)v;return 1;}
 return 0;
}
void SDIO_IRQHandler(void){SD_ProcessIRQSrc();}

