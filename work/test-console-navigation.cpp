#include <string>
#include <vector>
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
using std::max;using std::min;
struct String:std::string{using std::string::string;using std::string::operator=;String()=default;String(const std::string&s):std::string(s){}String(int i):std::string(std::to_string(i)){}bool startsWith(const char*s)const{return find(s)==0;}String substring(size_t a)const{return substr(a);}long toInt()const{return std::strtol(c_str(),0,10);}void trim(){auto i=find_first_not_of(" \r\n\t"),j=find_last_not_of(" \r\n\t");*this=i==npos?"":substr(i,j-i+1);}};
template<class T>T constrain(T a,T b,T c){return std::max(b,std::min(a,c));}
enum{SOURCE_ONLINE,SOURCE_FAVORITE,SOURCE_CARD};
static bool consoleFinishedEvent,saveMode,playbackActive=true,playbackPaused,stopPlayback,cardLocal,seekRestart,favoriteHasNext=true,stayOnList;
static int lyricSelected;static uint32_t trackPositionMs;
static int playMode,queuedOnlinePlay=-1,favoritePlay=-1,favoriteRequestPage,sourcePages[3],browsePage,browseSource=2,activeSource=2,cardCount=12,favoriteCount=8,favoritePage=1,selectedSong=0,musicCount=3;
static uint32_t trackDurationMs=120000,seekTargetMs,trackBaseMs;static char afterPlayback;
static String consoleAction,consoleJob,consoleTitle="Song",cardRequest,queuedDownloadHash,localFilter;
static struct{String hash;}musicResults[3];static struct{bool ready=true;uint32_t durationMs;}seekInfo;
static std::vector<String> frames;static int syncCalls;
static bool uiFrame(const String&s){frames.push_back(s);return true;}static String consoleHex(const String&s){return s;}
static void consoleFinish(bool){}static void consoleProgress(bool=false){}static void syncConsoleList(){syncCalls++;}
static void queueFollowing(bool,int){}static bool cardCommand(String,String,unsigned){return true;}static void consoleMessage(String){}
static struct {void println(const char*){}}Serial;
static uint32_t seekReadOffset,seekDecodeBaseMs,seekStartedAt,submittedSamples,lastProgressAt,bufferedSamples,audioUiSkipped,audioPollFailures,maxUiWaitMs;
static bool seekApproximate,durationEstimated,trackSeeking;static String playbackError;
struct Mp3SeekPlan{uint32_t offset=0,decodeBaseMs=0;bool approximate=false;};
static Mp3SeekPlan seekPlan(void*,uint32_t){return Mp3SeekPlan();}static uint32_t millis(){return 0;}
static void serviceConsole(){
 if(consoleFinishedEvent){consoleFinishedEvent=false;consoleFinish(true);}
 consoleProgress();
 if(!consoleAction.length())return;
 String action=consoleAction;consoleAction="";
 if(action=="MODE"){playMode=(playMode+1)%3;consoleProgress(true);return;}
 if(action=="NOW"){stayOnList=false;uiFrame("NV");lyricSelected=-2;consoleProgress(true);return;}
 if(action=="LIST"){stayOnList=true;syncConsoleList();return;}
 if(action=="DOWNLOAD"){if(activeSource==SOURCE_CARD){Serial.println("Download: already local");return;}if(selectedSong>=0&&selectedSong<musicCount){queuedDownloadHash=musicResults[selectedSong].hash;cardRequest="download:"+String(selectedSong);if(playbackActive){stopPlayback=true;afterPlayback='W';}}return;}
 if(action=="STOP"){
  queuedOnlinePlay=-1;favoritePlay=-1;favoriteRequestPage=0;consoleJob="";cardRequest="stop";stopPlayback=true;afterPlayback=0;return;
 }
 if(action=="PREV"){if(playbackActive){stopPlayback=true;afterPlayback='V';}else queueFollowing(true,-1);return;}
 if(action.startsWith("SEEK:")){
  if(!trackDurationMs||saveMode)return;
  long per=constrain(action.substring(5).toInt(),0L,1000L);
  uint32_t target=(uint32_t)((uint64_t)trackDurationMs*(unsigned long)per/1000);
  if(target>=trackDurationMs)target=trackDurationMs>1000?trackDurationMs-1000:0;
  if(cardLocal&&!playbackActive){
   if(cardCommand("J:"+String(target),"SD_SEEK:",3000)){trackBaseMs=target;trackPositionMs=target;consoleProgress(true);}return;
  }
  if(playbackActive&&!seekInfo.ready){consoleMessage("请等歌曲开始播放后再定位");return;}
  if(playbackActive){seekTargetMs=target;stopPlayback=true;afterPlayback='J';}
  return;
 }
 if(action.startsWith("SOURCE:")){
  int source=action.substring(7).toInt();if(source<0||source>2)return;
  sourcePages[browseSource]=browsePage;browseSource=source;browsePage=sourcePages[source];stayOnList=true;
  syncConsoleList();
  if(source==SOURCE_CARD&&!cardCount)consoleJob="CARD";
  else if(source==SOURCE_FAVORITE&&!favoriteCount)consoleJob="FAV:1";
  return;
 }else if(action.startsWith("PAGE:")){
  int step=action.substring(5).toInt()<0?-1:1;
  if(browseSource==SOURCE_FAVORITE){if(step>0&&!favoriteHasNext)return;consoleJob="FAV:"+String(max(1,favoritePage+step));}
  else {browsePage=max(0,browsePage+step);syncConsoleList();return;}
 }else if(action=="REFRESH")consoleJob=browseSource==SOURCE_CARD?"CARD":browseSource==SOURCE_FAVORITE?"FAV:1":"SEARCH:轻音乐";
 else if(action.startsWith("PRESET:")){
  const char* preset[]={"小苹果","小星星","轻音乐","钢琴"};int n=action.substring(7).toInt();if(n<0||n>3)return;if(browseSource==SOURCE_CARD){localFilter=preset[n];browsePage=0;syncConsoleList();return;}consoleJob="SEARCH:"+String(preset[n]);
 }else if(action.startsWith("SEARCH:")){String query=action.substring(7);query.trim();
  if(browseSource==SOURCE_CARD){localFilter=query;browsePage=0;syncConsoleList();return;}
  if(!query.length())return;consoleJob="SEARCH:"+query;}
 else if(action.startsWith("PLAY:")){stayOnList=false;consoleJob=action;}
 else return;
 if(playbackActive){stopPlayback=true;afterPlayback='W';}
}

static void consoleBegin(const String& title,uint32_t duration,bool estimated){
 playbackError="";
 bool restart=seekRestart;
 if(!restart){memset(&seekInfo,0,sizeof(seekInfo));seekInfo.durationMs=duration;seekReadOffset=seekDecodeBaseMs=0;seekApproximate=false;}
 else {Mp3SeekPlan plan=seekPlan(&seekInfo,seekTargetMs);seekReadOffset=plan.offset;seekDecodeBaseMs=plan.decodeBaseMs;seekApproximate=plan.approximate;}
 seekStartedAt=millis();
 consoleTitle=title;trackBaseMs=restart?seekTargetMs:0;trackPositionMs=trackBaseMs;
 trackDurationMs=duration;durationEstimated=estimated;trackSeeking=trackBaseMs>0;seekRestart=false;
 submittedSamples=0;lastProgressAt=0;lyricSelected=-2;bufferedSamples=audioUiSkipped=audioPollFailures=maxUiWaitMs=0;
 uiFrame(String(stayOnList?"NM:":"NP:")+consoleHex(title));
}
static void act(const char*s){consoleAction=s;serviceConsole();}
int main(){browsePage=2;act("SOURCE:0");assert(browseSource==0&&browsePage==0&&stayOnList&&!stopPlayback&&syncCalls==1);browsePage=1;
act("SOURCE:2");assert(browseSource==2&&browsePage==2&&activeSource==2&&!stopPlayback);act("SOURCE:1");assert(browseSource==1&&!stopPlayback);
act("NOW");assert(!stayOnList&&frames.back()=="NV"&&!stopPlayback);act("LIST");assert(stayOnList&&!stopPlayback);
act("PLAY:2:3");assert(!stayOnList&&stopPlayback&&afterPlayback=='W'&&consoleJob=="PLAY:2:3");
stopPlayback=false;activeSource=0;musicResults[0].hash="abc";act("DOWNLOAD");assert(stopPlayback&&cardRequest=="download:0"&&queuedDownloadHash=="abc");
stayOnList=true;consoleBegin("next",123000,false);assert(frames.back()=="NM:next");stayOnList=false;consoleBegin("selected",123000,false);assert(frames.back()=="NP:selected");
puts("Actual console action tests passed: source tabs/list/now preserve playing track; per-source page restored; explicit play/download interrupts deliberately");}
