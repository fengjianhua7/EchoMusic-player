#pragma once
static int selectedSong=0,presetIndex=0;
static bool playbackActive=false,playbackPaused=false,stopPlayback=false;
static char boardCommand=0,afterPlayback=0;
static String boardLine;
static void readBoardEvents() {
 while(screenLink.available()) {
  char c=(char)screenLink.read();
  if(c=='\n') {
   boardLine.trim();
   if(boardLine.length()==1 && String("NPLR").indexOf(boardLine[0])>=0)boardCommand=boardLine[0];
   boardLine="";
  } else if(boardLine.length()<48)boardLine+=c;
 }
}
static bool uiFrame(const String& frame) {
 screenLink.println(frame);screenLink.flush();
 uint32_t start=millis();String ack;
 while(millis()-start<1800) {
  while(screenLink.available()) {
   char c=(char)screenLink.read();
   if(c=='\n') {
    ack.trim();if(ack=="UI_OK")return true;
    if(ack.length()==1 && String("NPLR").indexOf(ack[0])>=0)boardCommand=ack[0];
    ack="";
   } else if(ack.length()<48)ack+=c;
  }
  delay(1);
 }
 Serial.println("UI update not confirmed");return false;
}
static void syncSongList() {
 selectedSong=0;
 if(!uiFrame("B:"+String(musicCount)))return;
 for(int i=0;i<musicCount;i++) {
  String title=musicResults[i].title;
  title.replace("<em>","");title.replace("</em>","");
  if(title.length()>120) {
   size_t cut=120;while(cut>0 && (((uint8_t)title[cut]&0xc0)==0x80))cut--;
   title=title.substring(0,cut);
  }
  String frame="T:"+String(i)+":";
  for(size_t j=0;j<title.length();j++) {
   char h[3];snprintf(h,sizeof(h),"%02X",(unsigned)(uint8_t)title[j]);frame+=h;
  }
  if(!uiFrame(frame))return;
 }
 uiFrame("C:0");screenStatus(musicCount?"Ready - select a song":"No matching results");
}
static bool playbackGate() {
 if(!playbackActive)return true;
 do {
  readBoardEvents();
  char c=boardCommand;boardCommand=0;
  if(c=='P') {
   playbackPaused=!playbackPaused;
   screenStatus(playbackPaused?"Paused - K2 resume":"Playing");
  } else if(c=='N'||c=='L'||c=='R') {stopPlayback=true;afterPlayback=c;}
  if(stopPlayback)return false;
  if(playbackPaused)delay(10);
 } while(playbackPaused);
 return true;
}
static void playSelectedSong(int index) {
 if(index<0||index>=musicCount)return;
 selectedSong=index;
 do {
  uiFrame("C:"+String(selectedSong));
  screenStatus("Getting playback URL...");
  String url=resolveMusic(selectedSong);
  if(!url.length()){screenStatus("Song unavailable - K1 next");return;}
  qrKey=""; // Stop QR status polling once playback starts.
  playbackActive=true;playbackPaused=false;stopPlayback=false;afterPlayback=0;
  screenStatus("Playing - K2 pause");
  bool ok=playUrl(url);
  playbackActive=false;playbackPaused=false;
  if(afterPlayback=='N'){selectedSong=(selectedSong+1)%musicCount;continue;}
  if(afterPlayback=='R'||afterPlayback=='L')boardCommand=afterPlayback;
  screenStatus(stopPlayback?"Stopped":(ok?"Finished":"Unable to play this version"));
  break;
 } while(true);
}
static void searchPreset() {
 static const char* presets[]={"小苹果","小星星","轻音乐","钢琴"};
 screenStatus("Searching...");
 searchMusic(presets[presetIndex]);
 presetIndex=(presetIndex+1)%4;
 syncSongList();
}
static void dispatchBoardCommand() {
 readBoardEvents();
 char c=boardCommand;boardCommand=0;
 if(c=='N' && musicCount) {selectedSong=(selectedSong+1)%musicCount;uiFrame("C:"+String(selectedSong));}
 else if(c=='P') {
  if(musicCount)playSelectedSong(selectedSong);else searchPreset();
 } else if(c=='R')searchPreset();
 else if(c=='L')beginQrLogin();
}
