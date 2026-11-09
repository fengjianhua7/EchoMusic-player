#pragma once
static String consoleJob,localFilter;
static void onConsoleLogin(){browseSource=SOURCE_FAVORITE;browsePage=0;consoleJob="FAV:1";}
static void replayLocal(){if(cardCount)cardRequest="play:"+String(max(0,cardIndex));}
static int localFiltered[128],localFilteredCount=0;
static String consoleHex(const String& text){
 String result;size_t size=text.length();if(size>120){size=120;while(size&&((uint8_t)text[size]&0xc0)==0x80)size--;}
 for(size_t i=0;i<size;i++){char h[3];snprintf(h,sizeof(h),"%02X",(unsigned)(uint8_t)text[i]);result+=h;}return result;
}
static void consoleMessage(const String& text){uiFrame("MSG:"+consoleHex(text));}
static bool handleTouchCommand(const String& line){if(!line.startsWith("UI:"))return false;consoleAction=line.substring(3);return true;}
static void syncConsoleList(){
 localFilteredCount=0;
 for(int i=0;i<cardCount;i++){String name=cardNames[i],query=localFilter;name.toLowerCase();query.toLowerCase();if(!query.length()||name.indexOf(query)>=0)localFiltered[localFilteredCount++]=i;}
 int total=browseSource==SOURCE_CARD?localFilteredCount:browseSource==SOURCE_FAVORITE?favoriteCount:searchCount;
 int page=browseSource==SOURCE_FAVORITE?max(0,favoritePage-1):browsePage;
 int start=browseSource==SOURCE_FAVORITE?0:page*8;
 if(start>=total&&browseSource!=SOURCE_FAVORITE){browsePage=0;page=0;start=0;}
 int count=min(8,max(0,total-start));
 bool next=browseSource==SOURCE_FAVORITE?favoriteHasNext:start+count<total;
 if(!uiFrame("LB:"+String(browseSource)+":"+String(page)+":"+String(total)+":"+String(count)+":"+String(next?1:0)))return;
 for(int i=0;i<count;i++){
  String title=browseSource==SOURCE_CARD?cardNames[localFiltered[start+i]]:browseSource==SOURCE_FAVORITE?favoriteSongs[i].title:searchResults[start+i].title;
  title.replace("<em>","");title.replace("</em>","");
  if(!uiFrame("LT:"+String(i)+":"+consoleHex(title)))return;
 }
 consoleProgress(true);
}
static void consoleBegin(const String& title,uint32_t duration,bool estimated){
 playbackError="";
 bool restart=seekRestart;
 if(!restart){memset(&seekInfo,0,sizeof(seekInfo));seekInfo.durationMs=duration;seekReadOffset=seekDecodeBaseMs=0;seekApproximate=false;}
 else {Mp3SeekPlan plan=seekPlan(&seekInfo,seekTargetMs);seekReadOffset=plan.offset;seekDecodeBaseMs=plan.decodeBaseMs;seekApproximate=plan.approximate;}
 seekStartedAt=millis();
 consoleTitle=title;trackBaseMs=restart?seekTargetMs:0;trackPositionMs=trackBaseMs;
 trackDurationMs=duration;durationEstimated=estimated;trackSeeking=trackBaseMs>0;seekRestart=false;
 submittedSamples=0;lastProgressAt=0;lyricSelected=-2;
 uiFrame("NP:"+consoleHex(title));
}
static void consoleProgress(bool force){
 if(saveMode)return;
 if(!force&&(!playbackActive&&!cardLocal))return;
 if(!force&&millis()-lastProgressAt<750)return;
 lastProgressAt=millis();
 String frame="PG:"+String(trackPositionMs)+":"+String(trackDurationMs)+":"+String(playbackPaused?1:0)+":"+String(volumePercent)+":"+String(playMode)+":"+String((durationEstimated||seekApproximate)?1:0)+":"+String(trackSeeking?1:0)+":"+String(trackBaseMs)+":"+String((playbackActive||cardLocal)?1:0);
 if(uiFrame(frame))lyricProgress();
}
static void consoleFinish(bool success){trackSeeking=false;consoleProgress(true);screenStatus(success&&!stopPlayback?"Finished":"Stopped");if(!success&&!stopPlayback)consoleMessage(playbackError.length()?playbackError:String("播放失败，请检查文件格式或传输接线"));}
static void finishAudio(){
 if(saveMode)return;
 uint32_t start=millis();
 while(millis()-start<1200){
  if(!playbackGate())return;
  consoleProgress(true);
  uint32_t played=trackPositionMs>=trackBaseMs?(trackPositionMs-trackBaseMs)*32:0;
  if(played>=submittedSamples)return;
  delay(10);
 }
}
static void queueFollowing(bool explicitNext,int direction){
 if(!explicitNext&&(!autoPlay||playMode==2))return;
 bool repeat=!explicitNext&&playMode==1;
 seekRestart=false;seekTargetMs=0;
 if(activeSource==SOURCE_CARD){
  if(cardCount){int i=repeat?cardIndex:(cardIndex+direction+cardCount)%cardCount;
   if(!repeat&&localFilter.length()&&localFilteredCount){int at=-1;for(int k=0;k<localFilteredCount;k++)if(localFiltered[k]==cardIndex)at=k;i=at<0?localFiltered[direction>0?0:localFilteredCount-1]:localFiltered[(at+direction+localFilteredCount)%localFilteredCount];}
   cardRequest="play:"+String(i);}
 }else if(activeSource==SOURCE_FAVORITE){
  int next=repeat?selectedSong:selectedSong+direction;
  if(next>=favoriteCount){favoriteRequestPage=favoriteHasNext?favoritePage+1:1;favoritePlay=0;}
  else if(next<0){if(favoritePage>1){favoriteRequestPage=favoritePage-1;favoritePlay=7;}else favoritePlay=max(0,favoriteCount-1);}
  else queuedOnlinePlay=next;
 }else if(musicCount){queuedOnlinePlay=repeat?selectedSong:(selectedSong+direction+musicCount)%musicCount;}
}
static void serviceConsole(){
 if(consoleFinishedEvent){consoleFinishedEvent=false;consoleFinish(true);}
 consoleProgress();
 if(!consoleAction.length())return;
 String action=consoleAction;consoleAction="";
 if(action=="MODE"){playMode=(playMode+1)%3;consoleProgress(true);return;}
 if(action=="NOW"){uiFrame("NP:"+consoleHex(consoleTitle));lyricSelected=-2;consoleProgress(true);return;}
 if(action=="LIST"){syncConsoleList();return;}
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
  browseSource=source;browsePage=0;
  if(source==SOURCE_CARD)consoleJob="CARD";
  else if(source==SOURCE_FAVORITE&&!favoriteCount)consoleJob="FAV:1";
  else {syncConsoleList();return;}
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
 else if(action.startsWith("PLAY:"))consoleJob=action;
 else return;
 if(playbackActive){stopPlayback=true;afterPlayback='W';}
}
static void processConsoleActions(){
 if(playbackActive||saveMode||!consoleJob.length())return;
 String job=consoleJob;consoleJob="";
 if(job=="CARD"){if(!cardCommand("X","SD_STOPPED",3000)){syncConsoleList();consoleMessage(cardStatus);return;}cardLocal=false;bool ok=cardCommand("V","SD_LIST_END",20000);syncConsoleList();if(!ok)uiFrame("MSG:"+consoleHex(cardStatus));}
 else if(job.startsWith("FAV:")){
  stopCardLocal();if(WiFi.status()!=WL_CONNECTED||time(nullptr)<1700000000){syncConsoleList();consoleMessage("等待 Wi-Fi 连接及时间同步后再读取喜欢列表");return;}checkFavoriteAccount();loadFavorites(max(1,(int)job.substring(4).toInt()));syncConsoleList();if(!favoriteCount)uiFrame("MSG:"+consoleHex(favoriteStatus));
 }else if(job.startsWith("SEARCH:")){
  stopCardLocal();browseSource=SOURCE_ONLINE;activeSource=SOURCE_ONLINE;browsePage=0;if(WiFi.status()!=WL_CONNECTED){syncConsoleList();uiFrame("MSG:"+consoleHex("Wi-Fi 未连接，请使用本地曲库"));return;}if(time(nullptr)<1700000000){syncConsoleList();consoleMessage("正在同步网络时间，请稍后再试");return;}screenStatus("Searching...");searchMusic(job.substring(7));syncConsoleList();
 }else if(job.startsWith("PLAY:")){
  int split=job.indexOf(':',5);if(split<0)return;
  int source=job.substring(5,split).toInt(),index=job.substring(split+1).toInt();
  seekRestart=false;seekTargetMs=0;
  if(source==SOURCE_CARD){if(index>=0&&index<localFilteredCount){activeSource=SOURCE_CARD;cardRequest="play:"+String(localFiltered[index]);}}
  else if(source==SOURCE_FAVORITE){index-=max(0,favoritePage-1)*8;if(index>=0&&index<favoriteCount){favoritePlay=index;activeSource=SOURCE_FAVORITE;}}
  else if(source==SOURCE_ONLINE&&index>=0&&index<searchCount){
   musicCount=searchCount;for(int i=0;i<searchCount;i++)musicResults[i]=searchResults[i];activeSource=SOURCE_ONLINE;queuedOnlinePlay=index;
  }
 }
}
