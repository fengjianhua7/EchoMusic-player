#pragma once
static String cardNames[128];
static uint32_t cardSizes[128];
static int cardCount=0,cardIndex=-1;
static uint32_t cardRevision=0;
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
  if(colon>8&&cardCount<128){cardNames[cardCount]=line.substring(8,colon);cardSizes[cardCount]=(uint32_t)line.substring(colon+1).toInt();cardCount++;}
 } else if(line=="SD_LIST_END"){cardRevision++;cardListing=false;cardStatus="内存卡列表已更新";}
 else if(line=="SD_PLAYING"){cardLocal=true;cardStatus="本地播放中";}
 else if(line=="SD_PAUSED"){cardStatus="本地已暂停";}
 else if(line=="SD_FINISHED"||line=="SD_STOPPED"){cardLocal=false;cardStatus="本地播放已停止";}
 else if(line.startsWith("SD_DONE:"))cardStatus="下载完成："+line.substring(8);
 else if(line.startsWith("SD_ERR:")){cardStatus="内存卡错误："+line.substring(7);cardListing=false;cardLocal=false;if(saveMode)stopPlayback=true;}
 cardReply=line;
 if(!line.startsWith("SD_BLOCK:"))Serial.println(line);return true;
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
    if(expected=="SD_PLAYING" && line.startsWith("SD_MP3:"))return true;
    if(line.length()==1 && String("NPLR+-12345").indexOf(line[0])>=0)boardCommand=line[0];
    line="";
   } else if(line.length()<1000)line+=c;
  }
  delay(0);
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
 if(req=="list"){stopCardLocal();cardCommand("V","SD_LIST_END",20000);}
 else if(req=="stop")stopCardLocal();
 else if(req.startsWith("play:")) {
  int i=req.substring(5).toInt();
  if(i>=0&&i<cardCount){
   cardIndex=i;
   if(cardCommand("I:"+String(i),"SD_PLAYING") && cardReply.startsWith("SD_MP3:"))
    playCardMp3(cardNames[i],(uint32_t)cardReply.substring(7).toInt());
  }
 } else if(req.startsWith("download:")) {
  int i=req.substring(9).toInt();
  if(i<0||i>=musicCount||musicResults[i].hash!=queuedDownloadHash){cardStatus="搜索列表已变化，请重新选择下载";return;}
  downloadSong(i);
 }
}
static void appendCardState(cJSON* root) {
 String nowTitle;
 if(cardLocal&&cardIndex>=0&&cardIndex<cardCount)nowTitle=cardNames[cardIndex];
 else if(selectedSong>=0&&selectedSong<musicCount)nowTitle=musicResults[selectedSong].title;
 cJSON_AddStringToObject(root,"nowTitle",nowTitle.c_str());
 cJSON_AddBoolToObject(root,"downloading",saveMode);cJSON_AddBoolToObject(root,"localPlaying",cardLocal);
 cJSON_AddStringToObject(root,"cardStatus",cardStatus.c_str());
 cJSON_AddNumberToObject(root,"downloadBytes",savedBytes);
 bool changed=phoneServer.arg("localRevision")!=String(cardRevision);
 cJSON_AddNumberToObject(root,"localRevision",cardRevision);
 cJSON_AddBoolToObject(root,"localChanged",changed);
 if(!changed)return;
 cJSON* sizes=cJSON_AddArrayToObject(root,"localFileSizes");
 for(int i=0;i<cardCount;i++)cJSON_AddItemToArray(sizes,cJSON_CreateNumber(cardSizes[i]));
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
   if(!index.length()||index.length()>3){phoneServer.send(400,"text/plain","Invalid index");return;}
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

class CardSpiStream : public Stream {
 uint32_t length,position=0;uint8_t incoming[512],dummy[512]={0};unsigned at=0,valid=0;
 bool failed=false;
 uint32_t transfers=0,totalUs=0,worstUs=0;
 bool refill() {
  if(failed || position>=length)return false;
  uint32_t started=micros();
  if(!cardCommand("B","SD_BLOCK:")){failed=true;return false;}
  unsigned n=0;unsigned long check=0;
  if(sscanf(cardReply.c_str(),"SD_BLOCK:%u:%lx",&n,&check)!=2||!n||n>512||n>length-position){failed=true;return false;}
  if(!waitReady(HIGH)){failed=true;return false;}
  SPI.beginTransaction(SPISettings(2000000,MSBFIRST,SPI_MODE0));digitalWrite(AUDIO_CS_PIN,LOW);
  SPI.transferBytes(dummy,incoming,512);
  bool ack=waitReady(LOW);digitalWrite(AUDIO_CS_PIN,HIGH);SPI.endTransaction();
  if(!ack || (cardCrc(0xffffffffUL,incoming,512)^0xffffffffUL)!=(uint32_t)check){
   Serial.printf("Local MP3 transfer failed: ACK=%d CRC expected=%08lX actual=%08lX\n",ack?1:0,check,(unsigned long)(cardCrc(0xffffffffUL,incoming,512)^0xffffffffUL));failed=true;return false;
  }
  uint32_t elapsed=micros()-started;transfers++;totalUs+=elapsed;if(elapsed>worstUs)worstUs=elapsed;
  if(transfers%256==0)Serial.printf("MP3 card transfer: blocks=%lu average=%lu us worst=%lu us\n",(unsigned long)transfers,(unsigned long)(totalUs/transfers),(unsigned long)worstUs);
  at=0;valid=n;return true;
 }
public:
 CardSpiStream(uint32_t n):length(n){}
 int available() override{return failed?0:(int)(length-position);}
 int read() override{if(at==valid&&!refill())return -1;position++;return incoming[at++];}
 int peek() override{if(at==valid&&!refill())return -1;return incoming[at];}
 void flush() override{}
 size_t write(uint8_t) override{return 0;}
 bool hasError() const{return failed;}
};
static bool playCardMp3(const String& name,uint32_t size) {
 if(!size||size>0x7fffffffUL){cardCommand("X","SD_STOPPED");cardStatus="MP3文件大小无效";return false;}
 Serial.printf("Local MP3 streaming: file=%lu, PSRAM total=%u free=%u\n",(unsigned long)size,ESP.getPsramSize(),ESP.getFreePsram());
 CardSpiStream stream(size);
 cardLocal=true;playbackActive=true;playbackPaused=false;stopPlayback=false;afterPlayback=0;
 cardStatus="本地MP3流式播放中";screenStatus("Playing local MP3 stream");
 cardCommand("Z","SD_STATS_RESET");
 bool ok=mp3(stream,(int)size);
 cardCommand("U","SD_UNDERRUN:");
 bool transferError=stream.hasError();
 playbackActive=false;playbackPaused=false;
 cardCommand("X","SD_STOPPED");cardLocal=false;
 if(afterPlayback=='N')requestCardNext();
 else if(afterPlayback=='R'||afterPlayback=='L'||(afterPlayback>='1'&&afterPlayback<='5'))boardCommand=afterPlayback;
 cardStatus=transferError?"MP3读卡或返回校验失败":(ok?"本地MP3播放结束":"本地MP3已停止或解码失败");
 return ok&&!transferError;
}
