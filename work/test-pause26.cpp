#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <algorithm>
using std::min;using std::max;
struct String:std::string{using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}};
template<class T>T constrain(T x,T a,T b){return max(a,min(b,x));}
enum{SOURCE_ONLINE,SOURCE_FAVORITE,SOURCE_CARD};
static bool playbackActive=true,playbackPaused=false,stopPlayback=false;static uint32_t clockMs,pausedAt,pausedPosition,trackPositionMs,seekTargetMs,lastProgressAt;static int activeSource=SOURCE_ONLINE,volumePercent=25;static char boardCommand='P',afterPlayback;
static String lastNetworkHash="hash",lastNetworkUrl="url";static struct{bool ready=true;}seekInfo;
static std::vector<String> commands;
static uint32_t millis(){return clockMs;}static void delay(unsigned ms){clockMs+=ms;}
static void serviceNetworkStartup(){}static void servicePhoneUi(){if(playbackPaused&&clockMs>=12000)boardCommand='P';}static void readBoardEvents(){}static void serviceConsole(){}static void consoleProgress(bool=false){}static void pollAudioState(){trackPositionMs=5000;}
static bool cardCommand(String c,const char*,unsigned){commands.push_back(c);return true;}
static struct{void println(const char*){}}Serial;
static bool playbackGate(){
 if(!playbackActive)return true;
 do{
  serviceNetworkStartup();servicePhoneUi();readBoardEvents();serviceConsole();
  char c=boardCommand;boardCommand=0;
  if(c=='+'||c=='-'){volumePercent=constrain(volumePercent+(c=='+'?5:-5),0,100);lastProgressAt=0;}
  else if(c>='1'&&c<='5'){stopPlayback=true;afterPlayback=c;}
  else if(c=='P'){
   if(!playbackPaused){pollAudioState();pausedAt=millis();pausedPosition=trackPositionMs;playbackPaused=true;cardCommand("AP:1","SD_AUDIO_PAUSED",500);}
   else if(activeSource!=SOURCE_CARD&&seekInfo.ready&&millis()-pausedAt>=10000){seekTargetMs=pausedPosition;lastNetworkHash="";lastNetworkUrl="";stopPlayback=true;afterPlayback='J';playbackPaused=false;Serial.println("Long network pause: reconnecting at paused position");}
   else {playbackPaused=false;cardCommand("AP:0","SD_AUDIO_PAUSED",500);}consoleProgress(true);
  }
  else if(c=='N'||c=='V'||c=='L'||c=='R'){stopPlayback=true;afterPlayback=c;}
  if(stopPlayback)return false;
  consoleProgress();
  if(playbackPaused)delay(10);
 }while(playbackPaused);
 return true;
}

int main(){assert(!playbackGate());assert(stopPlayback&&afterPlayback=='J'&&seekTargetMs==5000&&!playbackPaused&&lastNetworkHash.empty());assert(commands.size()==1&&commands[0]=="AP:1");
activeSource=SOURCE_CARD;clockMs=0;boardCommand='P';stopPlayback=false;afterPlayback=0;commands.clear();assert(playbackGate()&&!stopPlayback&&!playbackPaused);assert(commands.size()==2&&commands[1]=="AP:0");puts("Actual pause gate passed: long network pause queues fresh connection at frozen position; local resumes held queue without reconnect");}
