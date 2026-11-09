#include <string>
#include <vector>
#include <cstdint>
#include <cassert>
#include <cstdio>
struct String:std::string{using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}};
static bool cardLocal,playbackActive,playbackPaused,stopPlayback,seekRestart,durationEstimated;static uint32_t seekReadOffset,seekDecodeBaseMs,trackBaseMs,trackDurationMs=100000,seekTargetMs;static char afterPlayback,boardCommand;static String cardStatus;static int decodes,finishes,begins,nexts;static bool failClear;static std::vector<String> commands;static std::vector<uint32_t> offsets;
static struct{unsigned getPsramSize(){return 8388608;}unsigned getFreePsram(){return 8000000;}}ESP;static struct{int printf(const char*,...){return 0;}}Serial;
class CardSpiStream{public:CardSpiStream(uint32_t,uint32_t at){offsets.push_back(at);}bool hasError(){return false;}};
static bool cardCommand(const String&s,const char*,unsigned=8000){commands.push_back(s);return !(failClear&&s=="C");}
static void consoleMessage(const String&){}static void screenStatus(const char*){}static void consoleFinish(bool){finishes++;}static void consoleBegin(const String&,uint32_t,bool){begins++;seekReadOffset=20000;trackBaseMs=seekTargetMs;seekDecodeBaseMs=9500;seekRestart=false;}static void queueFollowing(bool=false,int=1){nexts++;}
static bool mp3(CardSpiStream&,int){decodes++;if(decodes==1){afterPlayback='J';stopPlayback=true;seekTargetMs=10000;return false;}return true;}
static bool playCardMp3(const String& name,uint32_t size) {
 if(!size||size>0x7fffffffUL){cardCommand("X","SD_STOPPED");cardStatus="MP3文件大小无效";return false;}
 Serial.printf("Local MP3 streaming: file=%lu, PSRAM total=%u free=%u\n",(unsigned long)size,ESP.getPsramSize(),ESP.getFreePsram());
 for(;;){
  CardSpiStream stream(size,seekReadOffset);
  if(stream.hasError()){consoleMessage("本地定位失败，请重新读取内存卡");cardCommand("X","SD_STOPPED");return false;}
  cardLocal=true;playbackActive=true;playbackPaused=false;stopPlayback=false;afterPlayback=0;
  cardStatus="本地MP3流式播放中";screenStatus("Playing local MP3 stream");cardCommand("Z","SD_STATS_RESET");
  bool ok=mp3(stream,(int)(size-seekReadOffset));bool transferError=stream.hasError();
  playbackActive=false;playbackPaused=false;
  if(afterPlayback=='J'){
   if(!cardCommand("C","SD_CLEARED",1000)){consoleMessage("定位时清理音频失败，请重试");cardCommand("X","SD_STOPPED");cardLocal=false;return false;}
   seekRestart=true;consoleBegin(name,trackDurationMs,durationEstimated);
   Serial.printf("Local seek: target=%lu offset=%lu warmup=%lu ms\n",(unsigned long)trackBaseMs,(unsigned long)seekReadOffset,(unsigned long)(trackBaseMs-seekDecodeBaseMs));
   continue;
  }
  cardCommand("U","SD_UNDERRUN:");consoleFinish(ok);cardCommand("X","SD_STOPPED");cardLocal=false;
  if(afterPlayback=='N'||afterPlayback=='V')queueFollowing(true,afterPlayback=='V'?-1:1);
  else if(ok&&!stopPlayback)queueFollowing();
  else if(afterPlayback=='R'||afterPlayback=='L'||(afterPlayback>='1'&&afterPlayback<='5'))boardCommand=afterPlayback;
  cardStatus=transferError?"MP3读卡或返回校验失败":(ok?"本地MP3播放结束":"本地MP3已停止或解码失败");return ok&&!transferError;
 }
}

int main(){assert(playCardMp3("a.mp3",100000));assert(decodes==2&&begins==1&&finishes==1&&nexts==1&&offsets.size()==2&&offsets[0]==0&&offsets[1]==20000);unsigned clears=0,closes=0,stats=0;for(auto&s:commands){if(s=="C")clears++;if(s=="X")closes++;if(s=="U")stats++;assert(s.find("I:")!=0);}assert(clears==1&&closes==1&&stats==1);
commands.clear();offsets.clear();decodes=begins=finishes=0;failClear=true;assert(!playCardMp3("a.mp3",100000));assert(offsets.size()==1);puts("Actual local seek loop passed: one open stream reused, no intermediate close/stats/finish page, seek restart at offset, clear failure stops safely");}
