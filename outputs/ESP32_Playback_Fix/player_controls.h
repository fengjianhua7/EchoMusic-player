#pragma once
static void requestCardNext();
static int selectedSong=0,presetIndex=0;
static int volumePercent=25;
static bool playbackActive=false,playbackPaused=false,stopPlayback=false;
static char boardCommand=0,afterPlayback=0;
static String boardLine;
static void consumeBoardLine(const String& text){
 if(handleTouchCommand(text))return;
 if(processCardLine(text))return;
 if(text.length()==1&&String("NPLR+-12345V").indexOf(text[0])>=0)boardCommand=text[0];
}
static bool readLinkLine(String& result){
 while(screenLink.available()){
  char c=(char)screenLink.read();
  if(c=='\n'){boardLine.trim();result=boardLine;boardLine="";return true;}
  if(c!='\r'&&boardLine.length()<1100)boardLine+=c;
 }
 return false;
}
static void clearScreenRx(){boardLine="";while(screenLink.available())screenLink.read();}
static void readBoardEvents(){String message;while(readLinkLine(message))consumeBoardLine(message);}
static bool uiFrame(const String& frame){
 screenLink.println(frame);screenLink.flush();uint32_t start=millis();String ack;
 uint32_t budget=(playbackActive&&!playbackPaused)?((frame.startsWith("PG:")||frame.startsWith("LY:"))?40:120):1800;
 while(millis()-start<budget){
  while(readLinkLine(ack)){
   if(ack=="UI_OK"){if(playbackActive)maxUiWaitMs=max(maxUiWaitMs,(uint32_t)(millis()-start));return true;}
   if(ack.startsWith("UI_OK:")){if(playbackActive)maxUiWaitMs=max(maxUiWaitMs,(uint32_t)(millis()-start));trackPositionMs=trackBaseMs+(uint32_t)strtoul(ack.c_str()+6,nullptr,10)/32;return true;}
   consumeBoardLine(ack);
  }
  delay(0);
 }
 Serial.println("UI update not confirmed");return false;
}
static void syncSongList(){syncConsoleList();}
static bool playbackGate(){
 if(!playbackActive)return true;
 do{
  serviceNetworkStartup();servicePhoneUi();readBoardEvents();serviceConsole();
  char c=boardCommand;boardCommand=0;
  if(c=='+'||c=='-'){volumePercent=constrain(volumePercent+(c=='+'?5:-5),0,100);consoleProgress(true);}
  else if(c>='1'&&c<='5'){stopPlayback=true;afterPlayback=c;}
  else if(c=='P'){playbackPaused=!playbackPaused;consoleProgress(true);}
  else if(c=='N'||c=='V'||c=='L'||c=='R'){stopPlayback=true;afterPlayback=c;}
  if(stopPlayback)return false;
  consoleProgress();
  if(playbackPaused)delay(10);
 }while(playbackPaused);
 return true;
}
static void playSelectedSong(int index){
 stopCardLocal();if(index<0||index>=musicCount)return;
 selectedSong=index;
 if(activeSource==SOURCE_CARD)activeSource=SOURCE_ONLINE;
 screenStatus("Getting playback URL...");
 String url;
 if(seekRestart&&lastNetworkHash==musicResults[index].hash&&millis()-lastNetworkUrlAt<600000)url=lastNetworkUrl;
 else {url=resolveMusic(index);if(url.length()){lastNetworkUrl=url;lastNetworkHash=musicResults[index].hash;lastNetworkUrlAt=millis();}}if(!url.length()){screenStatus("Song unavailable");consoleMessage("歌曲地址不可用，请尝试其他版本");return;}
 qrKey="";
 if(!seekRestart)prepareNetworkLyrics(index);
 consoleBegin(musicResults[index].title,musicResults[index].durationMs);
 cardCommand("C","SD_CLEARED",3000);
 playbackActive=true;playbackPaused=false;stopPlayback=false;afterPlayback=0;
 consoleProgress(true);
 bool ok=playUrl(url);
 playbackActive=false;playbackPaused=false;
 consoleFinish(ok);
 if(afterPlayback=='J'){seekRestart=true;queuedOnlinePlay=index;}
 else if(afterPlayback=='N'||afterPlayback=='V')queueFollowing(true,afterPlayback=='V'?-1:1);
 else if(afterPlayback>='1'&&afterPlayback<='5')queuedOnlinePlay=afterPlayback-'1';
 else if(afterPlayback=='R'||afterPlayback=='L')boardCommand=afterPlayback;
 else if(ok&&!stopPlayback)queueFollowing();
}
static void searchPreset(){
 stopCardLocal();static const char* presets[]={"小苹果","小星星","轻音乐","钢琴"};
 screenStatus("Searching...");searchMusic(presets[presetIndex]);presetIndex=(presetIndex+1)%4;
 browseSource=SOURCE_ONLINE;activeSource=SOURCE_ONLINE;browsePage=0;syncSongList();
}
static void dispatchBoardCommand(){
 readBoardEvents();
 if(queuedOnlinePlay>=0&&!playbackActive){int i=queuedOnlinePlay;queuedOnlinePlay=-1;playSelectedSong(i);return;}
 char c=boardCommand;boardCommand=0;
 if(cardLocal&&c=='P'){cardCommand("H","SD_",3000);playbackPaused=!playbackPaused;consoleProgress(true);return;}
 if(cardLocal&&(c=='N'||c=='A'||c=='V')){queueFollowing(true,c=='V'?-1:1);return;}
 if(c=='+'||c=='-'){volumePercent=constrain(volumePercent+(c=='+'?5:-5),0,100);consoleProgress(true);}
 else if(c=='A'&&musicCount){queueFollowing(true);}
 else if(c>='1'&&c<='5')playSelectedSong(c-'1');
 else if(c=='N'&&activeSource==SOURCE_CARD){queueFollowing(true);}
 else if(c=='N'&&musicCount){selectedSong=(selectedSong+1)%musicCount;syncConsoleList();}
 else if(c=='P'){if(activeSource==SOURCE_CARD)replayLocal();else if(musicCount)playSelectedSong(selectedSong);else searchPreset();}
 else if(c=='R')searchPreset();else if(c=='L')beginQrLogin();
}
