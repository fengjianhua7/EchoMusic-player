#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct {uint32_t ms;char text[128];} LrcEntry;
/* UTF-8 LRC; multiple time tags, variable fractions, signed offset and sorting. */
static unsigned lrcParse(const char* input,LrcEntry* out,unsigned capacity) {
 unsigned count=0,i,j;int offset=0;
 const char* scan=input;const char* p;
 while((scan=strstr(scan,"[offset:"))!=0){offset=atoi(scan+8);scan+=8;}
 p=input;
 while(*p&&count<capacity){
  const char* end=strchr(p,'\n');const char* text;const char* tag=p;
  uint32_t times[8];unsigned tags=0;
  if(!end)end=p+strlen(p);
  if((unsigned char)p[0]==0xef&&(unsigned char)p[1]==0xbb&&(unsigned char)p[2]==0xbf)tag+=3;
  while(tag<end&&*tag=='['){
   const char* close=(const char*)memchr(tag,']',(size_t)(end-tag));const char* q=tag+1;
   unsigned minutes=0,seconds=0,fraction=0,digits=0;int valid=1;
   if(!close)break;
   if(q>=close||*q<'0'||*q>'9')break;
   while(q<close&&*q>='0'&&*q<='9'){minutes=minutes*10+(*q++-'0');if(minutes>1440){valid=0;break;}}
   if(!valid||q>=close||*q++!=':')break;
   if(q+2>close||q[0]<'0'||q[0]>'9'||q[1]<'0'||q[1]>'9')break;
   seconds=(q[0]-'0')*10+q[1]-'0';q+=2;
   if(q<close&&*q=='.'){q++;while(q<close&&*q>='0'&&*q<='9'){if(digits<3)fraction=fraction*10+(*q-'0');digits++;q++;}if(!digits)valid=0;}
   if(q!=close||seconds>=60)valid=0;
   if(!valid)break;
   if(digits==1)fraction*=100;else if(digits==2)fraction*=10;
   if(tags<8)times[tags++]=(minutes*60+seconds)*1000+fraction;
   tag=close+1;
  }
  text=tag;
  if(tags){
   size_t n=(size_t)(end-text);while(n&&(text[n-1]=='\r'||text[n-1]==' '))n--;
   if(n>=sizeof(out[0].text)){n=sizeof(out[0].text)-1;while(n&&((unsigned char)text[n]&0xc0)==0x80)n--;}
   for(i=0;i<tags&&count<capacity;i++){
    int64_t t=(int64_t)times[i]+offset;out[count].ms=t<0?0:(uint32_t)t;
    memcpy(out[count].text,text,n);out[count].text[n]=0;count++;
   }
  }
  p=*end?end+1:end;
 }
 for(i=1;i<count;i++){LrcEntry temp=out[i];j=i;while(j&&out[j-1].ms>temp.ms){out[j]=out[j-1];j--;}out[j]=temp;}
 return count;
}
static int lrcAt(const LrcEntry* lines,unsigned count,uint32_t ms){
 unsigned lo=0,hi=count;while(lo<hi){unsigned mid=(lo+hi)/2;if(lines[mid].ms<=ms)lo=mid+1;else hi=mid;}return (int)lo-1;
}
