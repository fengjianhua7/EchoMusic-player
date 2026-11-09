#include "stm32f10x.h"
#include "ff.h"
#include "audio_gain.h"
static AudioGain wavGain;
#include "sdio/bsp_sdio_sdcard.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
extern void Player_Reply(const char* text);
extern unsigned Audio_Free(void);
extern void Audio_Push(const uint8_t* pcm,unsigned count,unsigned volume);
extern void Audio_Clear(void);
extern void Audio_SetPaused(int value);
extern void Audio_CancelTransfer(void);
extern unsigned Audio_Queued(void);
extern uint32_t Audio_Underruns(void);
extern uint32_t Audio_PlayedSamples(void);
extern void Audio_ResetStats(void);
static FATFS fs;
static FIL media,downloadMarker;
static int mounted,mode,failed,paused;
static unsigned volume=25;
static uint32_t bytes,crc,left;
static int rawDownload;
static uint32_t rawDownloadSize;
static uint32_t wavStart,wavBytes;
static char temporary[24],target[24];
static char indexedNames[128][13];
static unsigned indexedCount;
static uint8_t block[512];
static uint32_t mp3ReadAhead[2048];
static unsigned mp3CacheAt,mp3CacheSize,downloadBuffered;
static int fileTransfer;
extern void Audio_PrepareReturn(const uint8_t* data);
static uint32_t storage_le32(const uint8_t* b){return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static void put32(uint8_t* b,uint32_t n){b[0]=n;b[1]=n>>8;b[2]=n>>16;b[3]=n>>24;}
/* Reflected CRC-32, same polynomial and initial/final values as before. */
static uint32_t crcAdd(uint32_t c,const uint8_t* b,unsigned n){
 static const uint32_t table[16]={0x00000000UL,0x1DB71064UL,0x3B6E20C8UL,0x26D930ACUL,0x76DC4190UL,0x6B6B51F4UL,0x4DB26158UL,0x5005713CUL,0xEDB88320UL,0xF00F9344UL,0xD6D6A3E8UL,0xCB61B38CUL,0x9B64C2B0UL,0x86D3D2D4UL,0xA00AE278UL,0xBDBDF21CUL};
 while(n--){c^=*b++;c=(c>>4)^table[c&15];c=(c>>4)^table[c&15];}return c;
}
static int mountCard(void){FRESULT r;if(mounted)return 1;r=f_mount(&fs,"0:",1);if(r!=FR_OK){char msg[48];snprintf(msg,sizeof(msg),"SD_ERR:MOUNT:%u\n",(unsigned)r);Player_Reply(msg);return 0;}mounted=1;return 1;}
static void stop(void){if(mode)f_close(&media);mode=0;paused=0;fileTransfer=0;Audio_Clear();Audio_CancelTransfer();}
int Storage_Downloading(void){return mode==1;}
int Storage_Local(void){return mode==2;}
int Storage_TakeReadTransfer(void){int flag=fileTransfer;fileTransfer=0;return flag;}
static int flushDownload(void){UINT n=0;if(!downloadBuffered)return 1;if(f_write(&media,mp3ReadAhead,downloadBuffered,&n)!=FR_OK||n!=downloadBuffered){failed=1;Player_Reply("SD_ERR:WRITE\n");return 0;}downloadBuffered=0;return 1;}
void Storage_Consume(const uint8_t* data){unsigned take=512,at=0,n;
 if(mode!=1||failed)return;
 if(rawDownload){if(bytes>=rawDownloadSize){failed=1;Player_Reply("SD_ERR:EXCESS_DATA\n");return;}if(rawDownloadSize-bytes<take)take=(unsigned)(rawDownloadSize-bytes);}
 while(at<take){n=sizeof(mp3ReadAhead)-downloadBuffered;if(n>take-at)n=take-at;memcpy(((uint8_t*)mp3ReadAhead)+downloadBuffered,data+at,n);downloadBuffered+=n;at+=n;if(downloadBuffered==sizeof(mp3ReadAhead)&&!flushDownload())return;}
 bytes+=take;crc=crcAdd(crc,data,take);
}
static int validName(const char* n){unsigned i,len=(unsigned)strlen(n);if(!len||len>12||n[0]=='.')return 0;for(i=0;i<len;i++)if(!((n[i]>='A'&&n[i]<='Z')||(n[i]>='a'&&n[i]<='z')||(n[i]>='0'&&n[i]<='9')||n[i]=='_'||n[i]=='.'||n[i]=='~'))return 0;return 1;}
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
 DIR dir;FILINFO info;size_t len;FRESULT result;unsigned seen=0,folders=0,other=0,incomplete=0;static char longName[256],display[768],response[900];
 if(mode){Player_Reply("SD_ERR:BUSY\n");return;}if(!mountCard())return;
 memset(&info,0,sizeof(info));info.lfname=longName;info.lfsize=sizeof(longName);
 result=f_opendir(&dir,"0:/");
 if(result!=FR_OK){mounted=0;snprintf(response,sizeof(response),"SD_ERR:LIST:%u\n",(unsigned)result);Player_Reply(response);return;}
 indexedCount=0;Player_Reply("SD_LIST_BEGIN\n");
 while(indexedCount<128){
  longName[0]=0;result=f_readdir(&dir,&info);
  if(result!=FR_OK){f_closedir(&dir);mounted=0;snprintf(response,sizeof(response),"SD_ERR:READDIR:%u\n",(unsigned)result);Player_Reply(response);return;}
  if(!info.fname[0])break;
  seen++;len=strlen(info.fname);if(info.fattrib&AM_DIR){folders++;continue;}if(len<4){other++;continue;}
  if(strcmp(info.fname+len-4,".WAV")&&strcmp(info.fname+len-4,".wav")&&strcmp(info.fname+len-4,".MP3")&&strcmp(info.fname+len-4,".mp3")){other++;continue;}
  if(len==10&&info.fname[0]=='M'&&info.fname[6]=='.'){char markerName[24];FILINFO pending;snprintf(markerName,sizeof(markerName),"0:/%.6s.TMP",info.fname);memset(&pending,0,sizeof(pending));if(f_stat(markerName,&pending)==FR_OK){incomplete++;continue;}}
  strncpy(indexedNames[indexedCount],info.fname,12);indexedNames[indexedCount][12]=0;
  utf8Name(longName[0]?longName:info.fname,display,sizeof(display));
  snprintf(response,sizeof(response),"SD_FILE:%s:%lu\n",display,(unsigned long)info.fsize);Player_Reply(response);indexedCount++;
 }
 f_closedir(&dir);snprintf(response,sizeof(response),"SD_SCAN:%u:%u:%u:%u:%u\n",seen,folders,other,incomplete,indexedCount);Player_Reply(response);snprintf(response,sizeof(response),"SD_LIST_COUNT:%u\n",indexedCount);Player_Reply(response);Player_Reply("SD_LIST_END\n");
}
static void openLocal(const char* name,int indexed){uint8_t h[16];UINT got;DWORD size;int format=0;char path[24];
 stop();if(!indexed && !validName(name)){Player_Reply("SD_ERR:NAME\n");return;}if(!mountCard())return;
 snprintf(path,sizeof(path),"0:/%s",name);if(f_open(&media,path,FA_READ)!=FR_OK){Player_Reply("SD_ERR:OPEN\n");return;}
 if(strlen(name)>=4 && (!strcmp(name+strlen(name)-4,".MP3")||!strcmp(name+strlen(name)-4,".mp3"))) {
  char response[48];mode=3;left=f_size(&media);mp3CacheAt=mp3CacheSize=0;
  snprintf(response,sizeof(response),"SD_MP3:%lu\n",(unsigned long)left);Player_Reply(response);return;
 }
 mode=2;audioGainInit(&wavGain,volume);
 if(f_read(&media,h,12,&got)!=FR_OK||got!=12||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4))goto invalid;
 while(f_tell(&media)+8<=f_size(&media)) {
  if(f_read(&media,h,8,&got)!=FR_OK||got!=8)goto invalid;
  size=storage_le32(h+4);if(size>f_size(&media)-f_tell(&media))goto invalid;
  if(!memcmp(h,"fmt ",4)) {
   if(size<16||f_read(&media,h,16,&got)!=FR_OK||got!=16)goto invalid;
   format=h[0]==1&&h[1]==0&&h[2]==1&&h[3]==0&&storage_le32(h+4)==32000&&h[12]==2&&h[14]==16;
   if(!format||f_lseek(&media,f_tell(&media)+size-16+(size&1))!=FR_OK)goto invalid;
  } else if(!memcmp(h,"data",4)) {if(!format||(size&1))goto invalid;left=size;wavStart=f_tell(&media);wavBytes=size;{char reply[48];snprintf(reply,sizeof(reply),"SD_PLAYING:%lu\n",(unsigned long)(size/64));Player_Reply(reply);}return;}
  else if(f_lseek(&media,f_tell(&media)+size+(size&1))!=FR_OK)goto invalid;
 }
invalid:stop();Player_Reply("SD_ERR:WAV_FORMAT\n");
}
void Storage_Poll(void){UINT got;unsigned n;if(mode!=2||paused||Audio_Free()<256)return;
 if(!left){if(Audio_Queued())return;f_close(&media);mode=0;Player_Reply("SD_FINISHED\n");return;}
 n=left>512?512:(unsigned)left;
 if(f_read(&media,block,n,&got)!=FR_OK||got!=n){stop();Player_Reply("SD_ERR:READ\n");return;}
 {unsigned i;for(i=0;i<n;i+=2){int16_t sample=(int16_t)((uint16_t)block[i]|((uint16_t)block[i+1]<<8));sample=audioGainApply(&wavGain,sample,volume);block[i]=(uint8_t)sample;block[i+1]=(uint8_t)((uint16_t)sample>>8);}}
 memset(block+n,0,512-n);left-=n;Audio_Push(block,256,100);
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
static void readLocalLyrics(unsigned index){
 static FIL lrc;static FILINFO info;static DIR dir;
 static char longName[256],path[280],reply[220];
 uint8_t chunk[96];UINT got;unsigned i,total=0;int found=0;size_t len;
 if(mode||index>=indexedCount){Player_Reply("SD_LRC_END\n");return;}
 memset(&info,0,sizeof(info));info.lfname=longName;info.lfsize=sizeof(longName);
 if(f_opendir(&dir,"0:/")!=FR_OK){Player_Reply("SD_LRC_END\n");return;}
 while(f_readdir(&dir,&info)==FR_OK&&info.fname[0]){
  if(!strcmp(info.fname,indexedNames[index])){snprintf(path,sizeof(path),"0:/%s",longName[0]?longName:info.fname);found=1;break;}
 }
 f_closedir(&dir);len=strlen(path);
 if(!found||len<4){Player_Reply("SD_LRC_END\n");return;}
 strcpy(path+len-4,".lrc");
 if(f_open(&lrc,path,FA_READ)!=FR_OK){strcpy(path+len-4,".LRC");if(f_open(&lrc,path,FA_READ)!=FR_OK){Player_Reply("SD_LRC_END\n");return;}}
 while(total<32768&&f_read(&lrc,chunk,sizeof(chunk),&got)==FR_OK&&got){
  if(total+got>32768)got=(UINT)(32768-total);
  strcpy(reply,"SD_LRC:");for(i=0;i<got;i++)sprintf(reply+7+i*2,"%02X",chunk[i]);
  reply[7+got*2]='\n';reply[8+got*2]=0;Player_Reply(reply);total+=got;
 }
 f_close(&lrc);Player_Reply("SD_LRC_END\n");
}
int Storage_Command(const char* line){
 if(!strncmp(line,"AP:",3)){Audio_SetPaused(atoi(line+3)!=0);Player_Reply("SD_AUDIO_PAUSED\n");return 1;}
 if(!strcmp(line,"BL")){Player_Reply(*(volatile uint32_t*)0x08007000UL==0x26324F54UL?"SD_BOOT:26\n":"SD_BOOT:0\n");return 1;}
 if(!strcmp(line,"AQ")){char report[64];snprintf(report,sizeof(report),"SD_AUDIO:%lu:%u\n",(unsigned long)Audio_PlayedSamples(),Audio_Queued());Player_Reply(report);return 1;}

 if(!strncmp(line,"LC:",3)){readLocalLyrics((unsigned)atoi(line+3));return 1;}
 if(!strcmp(line,"C")){Audio_Clear();Player_Reply("SD_CLEARED\n");return 1;}
 if(!strncmp(line,"J:",2)){
  unsigned long ms=strtoul(line+2,0,10);uint32_t offset;
  if(mode!=2){Player_Reply("SD_ERR:NO_WAV\n");return 1;}
  offset=ms>wavBytes/64?wavBytes:(uint32_t)ms*64;offset&=~1UL;
  if(f_lseek(&media,wavStart+offset)!=FR_OK){Player_Reply("SD_ERR:SEEK\n");return 1;}
  Audio_Clear();left=wavBytes-offset;Player_Reply("SD_SEEK:OK\n");return 1;
 }

 if(!strcmp(line,"Z")){Audio_ResetStats();Player_Reply("SD_STATS_RESET\n");return 1;}
 if(!strcmp(line,"U")){char report[48];snprintf(report,sizeof(report),"SD_UNDERRUN:%lu\n",(unsigned long)Audio_Underruns());Player_Reply(report);return 1;}
 if(!strncmp(line,"I:",2)){int index=atoi(line+2);if(index<0||(unsigned)index>=indexedCount)Player_Reply("SD_ERR:INDEX\n");else openLocal(indexedNames[index],1);return 1;}
 if(!strncmp(line,"K:",2)){
  uint32_t offset=(uint32_t)strtoul(line+2,0,10);char response[48];
  if(mode!=3||offset>=f_size(&media)){Player_Reply("SD_ERR:MP3_OFFSET\n");return 1;}
  if(f_lseek(&media,offset)!=FR_OK){Player_Reply("SD_ERR:SEEK\n");return 1;}
  left=f_size(&media)-offset;mp3CacheAt=mp3CacheSize=0;
  snprintf(response,sizeof(response),"SD_OFFSET:%lu\n",(unsigned long)offset);Player_Reply(response);return 1;
 }
 if(!strcmp(line,"B")){readMp3Block();return 1;}
 if(!strncmp(line,"FU:",3)){startDownload(2,(uint32_t)strtoul(line+3,0,10));return 1;}
 if(!strcmp(line,"RB")){Player_Reply("SD_REBOOTING\n");while(USART_GetFlagStatus(USART3,USART_FLAG_TC)==RESET){}NVIC_SystemReset();return 1;}
 if(!strncmp(line,"DM:",3)){startDownload(1,(uint32_t)strtoul(line+3,0,10));return 1;}
 if(!strcmp(line,"D")){startDownload(0,0);return 1;}
 if(!strncmp(line,"E:",2)){finishDownload(line+2);return 1;}
 if(!strcmp(line,"V")){listFiles();return 1;}
 if(!strncmp(line,"O:",2)){openLocal(line+2,0);return 1;}
 if(!strcmp(line,"X")){stop();Player_Reply("SD_STOPPED\n");return 1;}
 if(!strcmp(line,"H")){if(mode==2){paused=!paused;Audio_SetPaused(paused);Player_Reply(paused?"SD_PAUSED\n":"SD_PLAYING\n");}return 1;}
 if(!strncmp(line,"G:",2)){int v=atoi(line+2);if(v>=0&&v<=100)volume=(unsigned)v;Player_Reply("SD_VOLUME\n");return 1;}
 return 0;
}
void SDIO_IRQHandler(void){SD_ProcessIRQSrc();}

