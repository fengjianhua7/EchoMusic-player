#pragma once
static String cardNames[20];
static uint32_t cardSizes[20];
static int cardCount=0,cardIndex=-1;
static bool cardListing=false;
static String queuedDownloadHash;
static String cardReply,cardStatus="未读取内存卡",cardRequest;
static uint32_t savedBytes=0,savedCrc=0xffffffffUL;
static uint32_t cardCrc(uint32_t c,const uint8_t* b,unsigned n){for(unsigned i=0;i<n;i++){c^=b[i];for(int k=0;k<8;k++)c=(c>>1)^((c&1)?0xedb88320UL:0);}return c;}
static bool processCardLine(const String& line) {
 if(!line.startsWith("SD_"))return false;
 if(line=="SD_LIST_BEGIN"){cardCount=0;cardListing=true;}
 else if(line.startsWith("SD_FILE:")) {
  int colon=line.indexOf(':',8);
  if(colon>8&&cardCount<20){cardNames[cardCount]=line.substring(8,colon);cardSizes[cardCount]=(uint32_t)line.substring(colon+1).toInt();cardCount++;}
 } else if(line=="SD_LIST_END"){cardListing=false;cardStatus="内存卡列表已更新";}
 else if(line=="SD_PLAYING"){cardLocal=true;cardStatus="本地播放中";}
 else if(line=="SD_PAUSED"){cardStatus="本地已暂停";}
 else if(line=="SD_FINISHED"||line=="SD_STOPPED"){cardLocal=false;cardStatus="本地播放已停止";}
 else if(line.startsWith("SD_DONE:"))cardStatus="下载完成："+line.substring(8);
 else if(line.startsWith("SD_ERR:")){cardStatus="内存卡错误："+line.substring(7);cardListing=false;cardLocal=false;if(saveMode)stopPlayback=true;}
 cardReply=line;
 Serial.println(line);return true;
}
static bool cardCommand(const String& cmd,const String& expected,uint32_t timeout=8000) {
 cardReply="";screenLink.println(cmd);screenLink.flush();String line;uint32_t start=millis();
 while(millis()-start<timeout) {
  while(screenLink.available()) {
   char c=(char)screenLink.read();
   if(c=='\n') {
    line.trim();processCardLine(line);
    if(line.startsWith("SD_ERR:"))return false;
    if(line.startsWith(expected))return true;
    if(line.length()==1 && String("NPLR+-12345").indexOf(line[0])>=0)boardCommand=line[0];
    line="";
   } else if(line.length()<100)line+=c;
  }
  delay(1);
 }
 cardStatus="内存卡通信超时";return false;
}
static void stopCardLocal() {
 if(cardLocal){cardCommand("X","SD_STOPPED");cardLocal=false;}
}
static void requestCardNext(){if(cardCount)cardRequest="play:"+String((cardIndex+1)%cardCount);}
static void requestCardStop(){if(cardLocal)cardRequest="stop";}
static void downloadSong(int index) {
 if(index<0||index>=musicCount)return;
 stopCardLocal();String url=resolveMusic(index);
 if(!url.length()){cardStatus="未取得歌曲播放链接";return;}
 if(!cardCommand("D","SD_READY"))return;
 savedBytes=0;savedCrc=0xffffffffUL;saveMode=true;
 playbackActive=true;playbackPaused=false;stopPlayback=false;afterPlayback=0;
 cardStatus="正在下载为 WAV…";screenStatus("Downloading to TF card...");
 bool ok=playUrl(url);
 playbackActive=false;playbackPaused=false;saveMode=false;
 if(ok && savedBytes && !stopPlayback) {
  char finish[64];snprintf(finish,sizeof(finish),"E:%lu:%08lX",(unsigned long)savedBytes,(unsigned long)(savedCrc^0xffffffffUL));
  if(cardCommand(finish,"SD_DONE:",15000))screenStatus("Download saved to TF card");
 } else {cardCommand("X","SD_STOPPED");cardStatus="下载未完成，临时文件保留";screenStatus("Download incomplete");}
}
static void processCardRequest() {
 static int previousVolume=-1;
 if(previousVolume!=volumePercent){previousVolume=volumePercent;screenLink.printf("G:%d\n",volumePercent);}
 if(!cardRequest.length()||playbackActive||saveMode)return;
 String req=cardRequest;cardRequest="";
 if(req=="list"){stopCardLocal();cardCommand("V","SD_LIST_END");}
 else if(req=="stop")stopCardLocal();
 else if(req.startsWith("play:")) {
  int i=req.substring(5).toInt();
  if(i>=0&&i<cardCount){cardIndex=i;cardCommand("O:"+cardNames[i],"SD_PLAYING");}
 } else if(req.startsWith("download:")) {
  int i=req.substring(9).toInt();
  if(i<0||i>=musicCount||musicResults[i].hash!=queuedDownloadHash){cardStatus="搜索列表已变化，请重新选择下载";return;}
  downloadSong(i);
 }
}
static void appendCardState(cJSON* root) {
 cJSON_AddBoolToObject(root,"downloading",saveMode);cJSON_AddBoolToObject(root,"localPlaying",cardLocal);
 cJSON_AddStringToObject(root,"cardStatus",cardStatus.c_str());
 cJSON_AddNumberToObject(root,"downloadBytes",savedBytes);
 cJSON* list=cJSON_AddArrayToObject(root,"localFiles");
 for(int i=0;i<cardCount;i++)cJSON_AddItemToArray(list,cJSON_CreateString(cardNames[i].c_str()));
}
static void registerCardRoutes() {
 phoneServer.on("/api/card",HTTP_POST,[](){
  if(!acceptPhoneRequest())return;
  String action=phoneServer.arg("action");
  if(saveMode||cardRequest.length()){phoneServer.send(409,"text/plain","Storage busy");return;}
  if(action=="list")cardRequest="list";
  else if(action=="stop")cardRequest="stop";
  else if(action=="download"||action=="play") {
   String index=phoneServer.arg("index");
   if(!index.length()||index.length()>2){phoneServer.send(400,"text/plain","Invalid index");return;}
   for(size_t i=0;i<index.length();i++)if(index[i]<'0'||index[i]>'9'){phoneServer.send(400,"text/plain","Invalid index");return;}
   int n=index.toInt();int limit=action=="download"?musicCount:cardCount;
   if(n<0||n>=limit){phoneServer.send(400,"text/plain","Invalid index");return;}
   if(action=="download")queuedDownloadHash=musicResults[n].hash;
   cardRequest=(action=="download"?"download:":"play:")+index;
  } else {phoneServer.send(400,"text/plain","Invalid action");return;}
  if(playbackActive){stopPlayback=true;afterPlayback='W';}
  phoneServer.send(202,"text/plain","Queued");
 });
}
