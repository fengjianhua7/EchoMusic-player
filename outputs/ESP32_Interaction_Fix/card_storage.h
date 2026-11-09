#pragma once
static String cardNames[128];
static uint32_t cardSizes[128];
static int cardCount=0,cardIndex=-1;
static uint32_t cardRevision=0;
static bool cardListing=false;
static String queuedDownloadHash;
static String cardReply,cardStatus="未读取内存卡",cardRequest;
static unsigned downloadChunkBytes=512;
static uint32_t savedBytes=0,savedCrc=0xffffffffUL;
static uint32_t cardCrc(uint32_t c,const uint8_t* b,unsigned n){for(unsigned i=0;i<n;i++){c^=b[i];for(int k=0;k<8;k++)c=(c>>1)^((c&1)?0xedb88320UL:0);}return c;}
static bool processCardLine(const String& line) {
 if(!line.startsWith("SD_"))return false;
 if(line.startsWith("SD_LRC:")){receiveLocalLyric(line.substring(7));return true;}
 if(line=="SD_LIST_BEGIN"){cardCount=0;cardListing=true;}
 else if(line.startsWith("SD_FILE:")) {
  int colon=line.indexOf(':',8);
  if(colon>8&&cardCount<128){cardNames[cardCount]=line.substring(8,colon);cardSizes[cardCount]=(uint32_t)line.substring(colon+1).toInt();cardCount++;}
 } else if(line=="SD_LIST_END"){cardRevision++;cardListing=false;cardStatus=cardCount?("已读取 "+String(cardCount)+" 首本地歌曲"):String("根目录未找到 MP3/WAV，请检查文件位置");
  Serial.printf("TF list loaded: songs=%d\n",cardCount);}
 else if(line.startsWith("SD_PLAYING")){cardLocal=true;cardStatus="本地播放中";}
 else if(line=="SD_PAUSED"){cardStatus="本地已暂停";}
 else if(line=="SD_FINISHED"||line=="SD_STOPPED"){bool ended=line=="SD_FINISHED";if(ended)consoleFinishedEvent=true;cardLocal=false;playbackPaused=false;cardStatus="本地播放已停止";if(ended)queueFollowing();}
 else if(line.startsWith("SD_DONE:"))cardStatus="下载完成："+line.substring(8);
 else if(line.startsWith("SD_ERR:")){cardStatus="内存卡错误："+line.substring(7);cardListing=false;cardLocal=false;if(saveMode)stopPlayback=true;}
 cardReply=line;
 if(!line.startsWith("SD_BLOCK:")&&!line.startsWith("SD_AUDIO:"))Serial.println(line);return true;
}
static bool cardCommand(const String& cmd,const String& expected,uint32_t timeout){
 cardReply="";screenLink.println(cmd);screenLink.flush();String line;uint32_t start=millis();
 while(millis()-start<timeout){
  while(readLinkLine(line)){
   processCardLine(line);
   if(line.startsWith("SD_ERR:"))return false;
   if(line.startsWith(expected))return true;
   if(expected=="SD_PLAYING"&&line.startsWith("SD_MP3:"))return true;
   if(!line.startsWith("SD_"))consumeBoardLine(line);
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
 if(!url.length()){cardStatus="未取得歌曲下载链接";return;}
 NetworkClientSecure tls;tls.useBuiltinCACertBundle();tls.setHandshakeTimeout(15);
 NetworkClient plain;HTTPClient http;http.useHTTP10(true);http.setTimeout(10000);http.setConnectTimeout(15000);
 http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
 NetworkClient& client=url.startsWith("https:")?static_cast<NetworkClient&>(tls):plain;
 if(!http.begin(client,url)){cardStatus="下载连接初始化失败";return;}
 const char* headers[]={"Content-Encoding","Transfer-Encoding","Content-Type"};http.collectHeaders(headers,3);
 http.addHeader("Accept-Encoding","identity");int code=http.GET(),length=http.getSize();
 Serial.printf("MP3 download: HTTP=%d bytes=%d\n",code,length);
 if(code!=200||length<=0||http.header("Content-Encoding").length()||http.header("Transfer-Encoding").length()){
  cardStatus="下载响应不支持：HTTP "+String(code);http.end();return;
 }
 savedBytes=0;savedCrc=0xffffffffUL;saveMode=true;downloadChunkBytes=512;
 playbackActive=true;playbackPaused=false;stopPlayback=false;afterPlayback=0;
 cardStatus="正在下载 MP3 原文件…";consoleMessage(cardStatus);
 bool opened=false,ok=true;uint32_t remaining=(uint32_t)length,lastReport=0;
 while(remaining&&!stopPlayback){
  unsigned count=(unsigned)min(remaining,(uint32_t)512);
  if(!exact(*http.getStreamPtr(),packet,count)){ok=false;break;}
  if(!opened){
   bool isMp3=count>=3&&((packet[0]=='I'&&packet[1]=='D'&&packet[2]=='3')||(packet[0]==0xff&&(packet[1]&0xe0)==0xe0&&(packet[1]&6)==2));
   if(!isMp3){cardStatus="云端返回的不是可识别 MP3，未保存";ok=false;break;}
   if(!cardCommand("DM:"+String(length),"SD_READY",5000)){ok=false;break;}opened=true;
  }
  if(count<512)memset(packet+count,0,512-count);downloadChunkBytes=count;
  if(!sendBlock()){ok=false;break;}remaining-=count;
  if(millis()-lastReport>=1000){lastReport=millis();Serial.printf("MP3 download saved=%lu/%d\n",(unsigned long)savedBytes,length);}
 }
 http.end();playbackActive=false;playbackPaused=false;saveMode=false;downloadChunkBytes=512;
 if(ok&&opened&&!remaining&&!stopPlayback){
  char finish[64];snprintf(finish,sizeof(finish),"E:%lu:%08lX",(unsigned long)savedBytes,(unsigned long)(savedCrc^0xffffffffUL));
  if(cardCommand(finish,"SD_DONE:",15000)){String done=cardStatus;cardCommand("V","SD_LIST_END",20000);cardStatus=done;syncConsoleList();consoleMessage(done);}
 }else {String error=cardStatus;if(opened)cardCommand("X","SD_STOPPED");cardStatus=error.startsWith("云端")?error:String("MP3下载未完成，临时文件不列入曲库");consoleMessage(cardStatus);}
}
static void processCardRequest() {
 static int previousVolume=-1;
 if(previousVolume!=volumePercent){if(cardCommand("G:"+String(volumePercent),"SD_VOLUME",3000))previousVolume=volumePercent;}
 if(!cardRequest.length()||playbackActive||saveMode)return;
 String req=cardRequest;cardRequest="";
 if(req=="list"){stopCardLocal();cardCommand("V","SD_LIST_END",20000);}
 else if(req=="stop")stopCardLocal();
 else if(req.startsWith("play:")) {
  int i=req.substring(5).toInt();
  if(i>=0&&i<cardCount){
   stopCardLocal();
   if(!seekRestart){if(!stayOnList)consoleMessage("正在准备歌曲和歌词，请稍候");prepareLocalLyrics(i);}
   cardIndex=i;activeSource=SOURCE_CARD;
   if(cardCommand("I:"+String(i),"SD_PLAYING")){
    if(cardReply.startsWith("SD_MP3:")){
     uint32_t size=(uint32_t)cardReply.substring(7).toInt();
     uint32_t duration=seekRestart?trackDurationMs:0;
     consoleBegin(cardNames[i],duration,seekRestart&&durationEstimated);
     playCardMp3(cardNames[i],size);
    }else if(cardReply.startsWith("SD_PLAYING")){
     uint32_t duration=(uint32_t)cardReply.substring(11).toInt();
     stopPlayback=false;playbackPaused=false;consoleBegin(cardNames[i],duration);consoleProgress(true);
    }
   }
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
 cJSON_AddStringToObject(root,"nowTitle",consoleTitle.length()?consoleTitle.c_str():nowTitle.c_str());
 cJSON_AddNumberToObject(root,"positionMs",trackPositionMs);cJSON_AddNumberToObject(root,"durationMs",trackDurationMs);
 cJSON_AddNumberToObject(root,"playMode",playMode);cJSON_AddBoolToObject(root,"seeking",trackSeeking);
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
 CardSpiStream(uint32_t n,uint32_t offset=0):length(n){
  if(offset>=n){failed=true;return;}
  if(offset){if(!cardCommand("K:"+String(offset),"SD_OFFSET:")||(uint32_t)cardReply.substring(10).toInt()!=offset){failed=true;return;}position=offset;}
 }
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
 CardSpiStream stream(size,seekReadOffset);
 if(stream.hasError()){consoleMessage("本地定位失败，请重新读取内存卡");cardCommand("X","SD_STOPPED");return false;}
 cardLocal=true;playbackActive=true;playbackPaused=false;stopPlayback=false;afterPlayback=0;
 cardStatus="本地MP3流式播放中";screenStatus("Playing local MP3 stream");
 cardCommand("Z","SD_STATS_RESET");
 bool ok=mp3(stream,(int)(size-seekReadOffset));
 cardCommand("U","SD_UNDERRUN:");
 bool transferError=stream.hasError();
 playbackActive=false;playbackPaused=false;
 consoleFinish(ok);
 cardCommand("X","SD_STOPPED");cardLocal=false;
 if(afterPlayback=='J'){seekRestart=true;cardRequest="play:"+String(cardIndex);}
 else if(afterPlayback=='N'||afterPlayback=='V')queueFollowing(true,afterPlayback=='V'?-1:1);
 else if(ok&&!stopPlayback)queueFollowing();
 else if(afterPlayback=='R'||afterPlayback=='L'||(afterPlayback>='1'&&afterPlayback<='5'))boardCommand=afterPlayback;
 cardStatus=transferError?"MP3读卡或返回校验失败":(ok?"本地MP3播放结束":"本地MP3已停止或解码失败");
 return ok&&!transferError;
}
