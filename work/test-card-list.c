#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#define AM_DIR 16
#define FR_OK 0
typedef int FRESULT;typedef struct{unsigned at;}DIR;typedef struct{char fname[13];unsigned fattrib;uint32_t fsize;char* lfname;unsigned lfsize;}FILINFO;
static int mode,mounted=1,openError,readError,totalFiles=3,onlyMp3;static unsigned indexedCount;static char indexedNames[128][13],messages[30000];
void Player_Reply(const char* p){assert(strlen(messages)+strlen(p)<sizeof(messages));strcat(messages,p);}int mountCard(void){return 1;}
int f_opendir(DIR* d,const char* p){d->at=0;return openError;}int f_closedir(DIR*d){return 0;}
int f_readdir(DIR*d,FILINFO*f){unsigned i=d->at++;if(readError&&i==1)return 1;if(i>=(unsigned)totalFiles){f->fname[0]=0;return 0;}f->fattrib=0;f->fsize=1234;snprintf(f->fname,sizeof(f->fname),"F%04u.%s",i,onlyMp3||i<2?"MP3":"TXT");snprintf(f->lfname,f->lfsize,"Long song %u.%s",i,onlyMp3||i<2?"mp3":"txt");return 0;}
void utf8Name(const char* in,char*out,unsigned n){snprintf(out,n,"%s",in);}
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
  strncpy(indexedNames[indexedCount],info.fname,12);indexedNames[indexedCount][12]=0;
  utf8Name(longName[0]?longName:info.fname,display,sizeof(display));
  snprintf(response,sizeof(response),"SD_FILE:%s:%lu\n",display,(unsigned long)info.fsize);Player_Reply(response);indexedCount++;
 }
 f_closedir(&dir);Player_Reply("SD_LIST_END\n");
}

int main(void){listFiles();assert(indexedCount==2&&strstr(messages,"SD_FILE:Long song 1.mp3:1234")&&strstr(messages,"SD_LIST_END"));
 messages[0]=0;openError=3;listFiles();assert(strstr(messages,"SD_ERR:LIST:3")&&!strstr(messages,"SD_LIST_BEGIN")&&indexedCount==2&&!mounted);
 openError=0;readError=1;messages[0]=0;listFiles();assert(strstr(messages,"SD_ERR:READDIR:1")&&!strstr(messages,"SD_LIST_END"));
 readError=0;onlyMp3=1;totalFiles=140;messages[0]=0;listFiles();assert(indexedCount==128&&strstr(messages,"SD_LIST_END"));puts("Actual TF listing tests passed: LFN, extension filtering, explicit open/read errors, preserved old list, 128-file limit");}
