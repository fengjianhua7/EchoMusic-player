#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstdint>
struct String:std::string{using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}String(int n):std::string(std::to_string(n)){}bool startsWith(const char*s)const{return find(s)==0;}int indexOf(char c,size_t at=0)const{auto p=find(c,at);return p==npos?-1:(int)p;}String substring(size_t a)const{return substr(a);}String substring(size_t a,size_t b)const{return substr(a,b-a);}long toInt()const{return strtol(c_str(),0,10);}};
static bool cardLocal,playbackPaused,stopPlayback,consoleFinishedEvent,saveMode;
static void receiveLocalLyric(String){}static void queueFollowing(){}
static std::vector<String> debugLines;static struct{void println(const String&s){debugLines.push_back(s);}int printf(const char*,...){return 0;}}Serial;

static String cardNames[128];
static uint32_t cardSizes[128];
static int cardCount=0,cardIndex=-1;
static uint32_t cardRevision=0;
static bool cardListing=false,cardListComplete=false;
static String cardStagedNames[128];static uint32_t cardStagedSizes[128];
static int cardStagedCount=0,cardExpectedCount=-1;
static String queuedDownloadHash;
static String cardReply,cardStatus="未读取内存卡",cardRequest;
static unsigned downloadChunkBytes=512;
static uint32_t savedBytes=0,savedCrc=0xffffffffUL;
static uint32_t cardCrc(uint32_t c,const uint8_t* b,unsigned n){for(unsigned i=0;i<n;i++){c^=b[i];for(int k=0;k<8;k++)c=(c>>1)^((c&1)?0xedb88320UL:0);}return c;}
static bool processCardLine(const String& line) {
 if(!line.startsWith("SD_"))return false;
 if(line.startsWith("SD_LRC:")){receiveLocalLyric(line.substring(7));return true;}
 if(line=="SD_LIST_BEGIN"){cardStagedCount=0;cardExpectedCount=-1;cardListComplete=false;cardListing=true;}
 else if(cardListing&&line.startsWith("SD_LIST_COUNT:")){cardExpectedCount=line.substring(14).toInt();if(cardExpectedCount<0||cardExpectedCount>128)cardExpectedCount=-2;}
 else if(line.startsWith("SD_FILE:")) {
  int colon=line.indexOf(':',8);
  if(cardListing&&colon>8&&cardStagedCount<128){cardStagedNames[cardStagedCount]=line.substring(8,colon);cardStagedSizes[cardStagedCount]=(uint32_t)line.substring(colon+1).toInt();cardStagedCount++;}
 } else if(line=="SD_LIST_END"){
  if(!cardListing){cardListComplete=false;cardStatus="未收到完整曲库开始标记，请点本地重读";return true;}
  cardListing=false;
  if(cardExpectedCount==-2||(cardExpectedCount>=0&&cardStagedCount!=cardExpectedCount)){cardStatus="曲库列表接收不完整，请点本地重读";Serial.printf("TF list incomplete: received=%d expected=%d\n",cardStagedCount,cardExpectedCount);return true;}
  for(int i=0;i<cardStagedCount;i++){cardNames[i]=cardStagedNames[i];cardSizes[i]=cardStagedSizes[i];}cardCount=cardStagedCount;cardListComplete=true;cardRevision++;cardStatus=cardCount?("已读取 "+String(cardCount)+" 首本地歌曲"):String("根目录未找到 MP3/WAV，请检查文件位置");
  Serial.printf("TF list loaded: songs=%d\n",cardCount);}
 else if(line.startsWith("SD_PLAYING")){cardLocal=true;cardStatus="本地播放中";}
 else if(line=="SD_PAUSED"){cardStatus="本地已暂停";}
 else if(line=="SD_FINISHED"||line=="SD_STOPPED"){bool ended=line=="SD_FINISHED";if(ended)consoleFinishedEvent=true;cardLocal=false;playbackPaused=false;cardStatus="本地播放已停止";if(ended)queueFollowing();}
 else if(line.startsWith("SD_DONE:"))cardStatus="下载完成："+line.substring(8);
 else if(line.startsWith("SD_ERR:")){cardStatus="内存卡错误："+line.substring(7);cardListing=false;cardLocal=false;if(saveMode)stopPlayback=true;}
 cardReply=line;
 if(!line.startsWith("SD_BLOCK:")&&!line.startsWith("SD_AUDIO:")&&!line.startsWith("SD_FILE:"))Serial.println(line);return true;
}

int main(){cardCount=1;cardNames[0]="old";processCardLine("SD_LIST_BEGIN");processCardLine("SD_FILE:歌曲一.mp3:123");assert(cardCount==1&&cardNames[0]=="old");processCardLine("SD_FILE:歌曲二.mp3:456");processCardLine("SD_LIST_COUNT:2");processCardLine("SD_LIST_END");assert(cardCount==2&&cardListComplete&&cardNames[1]=="歌曲二.mp3");
for(auto&s:debugLines)assert(!s.startsWith("SD_FILE:"));
processCardLine("SD_LIST_BEGIN");processCardLine("SD_FILE:partial.mp3:4");processCardLine("SD_LIST_COUNT:3");processCardLine("SD_LIST_END");assert(cardCount==2&&!cardListComplete&&cardNames[1]=="歌曲二.mp3");
processCardLine("SD_LIST_END");assert(cardCount==2&&!cardListComplete);
processCardLine("SD_LIST_BEGIN");processCardLine("SD_FILE:legacy.mp3:5");processCardLine("SD_LIST_END");assert(cardCount==1&&cardListComplete&&cardNames[0]=="legacy.mp3");puts("Actual library parser passed: atomic commit, lost-row count detection, stray END rejected, legacy compatibility, per-file serial mirroring suppressed");}
