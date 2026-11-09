#pragma once
#include "lrc_core.h"
#include "mbedtls/base64.h"
static LrcEntry* lyricLines=nullptr;
static unsigned lyricCount=0;
static int lyricSelected=-2;
static String localLyricText,lyricCacheHash,lyricStatus="暂无同步歌词";
static void clearLyrics(){lyricCount=0;lyricSelected=-2;localLyricText="";lyricStatus="暂无同步歌词";}
static void parseLyrics(const String& text) {
 if(!lyricLines){lyricLines=(LrcEntry*)ps_malloc(sizeof(LrcEntry)*256);if(!lyricLines)lyricLines=(LrcEntry*)malloc(sizeof(LrcEntry)*256);}
 lyricCount=lyricLines?lrcParse(text.c_str(),lyricLines,256):0;lyricSelected=-2;
 Serial.printf("LRC lines: %u\n",lyricCount);
}
static void receiveLocalLyric(const String& hex) {
 if(localLyricText.length()+hex.length()/2>32768)return;
 for(size_t i=0;i+1<hex.length();i+=2){char h[3]={hex[i],hex[i+1],0};localLyricText+=(char)strtoul(h,nullptr,16);}
}
static void fetchNetworkLyrics(const String& hash,const String& audioId,const String& title,uint32_t duration);
static void prepareLocalLyrics(int index) {
 clearLyrics();lyricCacheHash="";
 if(cardCommand("LC:"+String(index),"SD_LRC_END",15000)&&localLyricText.length())parseLyrics(localLyricText);
 localLyricText="";
 if(!lyricCount&&WiFi.status()==WL_CONNECTED&&index>=0&&index<cardCount){
  String title=cardNames[index];if(title.length()>4)title.remove(title.length()-4);title.replace("&amp;","&");
  if(title!="test"&&title!="test1"&&!title.startsWith("M000"))fetchNetworkLyrics("","",title,0);
 }
}
// Public lyric service: no account token, cookie or device session is sent.
static cJSON* lyricRequest(const String& path,std::map<String,String> params) {
 String url="https://lyrics.kugou.com"+path+"?";
 for(const auto& p:params)url+=urlEncode(p.first)+"="+urlEncode(p.second)+"&";
 NetworkClientSecure tls;tls.setCACert(MUSIC_ROOT_CA);tls.setHandshakeTimeout(8);
 HTTPClient http;http.setTimeout(8000);http.setConnectTimeout(8000);http.useHTTP10(false);
 if(!http.begin(tls,url))return nullptr;
 http.addHeader("Accept-Encoding","identity");http.addHeader("User-Agent","Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi");int code=http.GET();
 if(code!=200||http.getSize()>65536){Serial.printf("LRC request HTTP %d\n",code);http.end();return nullptr;}
 String text=http.getString();http.end();
 Serial.printf("LRC %s: HTTP %d bytes=%u\n",path.c_str(),code,(unsigned)text.length());
 if(!text.length()||text.length()>65536)return nullptr;
 cJSON* result=cJSON_Parse(text.c_str());if(!result)Serial.println("LRC response is not JSON");return result;
}
static cJSON* lyricCandidates(cJSON* doc){
 cJSON* list=cJSON_GetObjectItemCaseSensitive(doc,"candidates");
 if(!cJSON_IsArray(list))list=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(doc,"data"),"candidates");return list;
}
static void fetchNetworkLyrics(const String& hash,const String& audioId,const String& title,uint32_t duration){
 if(time(nullptr)<1700000000){lyricStatus="等待网络时间同步后重播获取歌词";return;}
 String cache=hash.length()?hash:md5Text(title);
 if(lyricCacheHash==cache&&lyricCount){lyricSelected=-2;return;}
 clearLyrics();lyricStatus="正在获取歌词";
 String query=title;query.replace("<em>","");query.replace("</em>","");query.replace("&amp;","&");
 cJSON* doc=lyricRequest("/search",{{"ver","1"},{"man","no"},{"client","pc"},{"lrctxt","1"},{"hash",hash},{"album_audio_id",audioId},{"duration",String(duration)},{"keyword",query}});
 if(!doc){lyricStatus="歌词服务请求失败";return;}
 cJSON* list=lyricCandidates(doc);
 if(!cJSON_GetArraySize(list)&&hash.length()){
  cJSON_Delete(doc);doc=lyricRequest("/search",{{"ver","1"},{"man","no"},{"client","pc"},{"lrctxt","1"},{"duration","0"},{"keyword",query}});
  if(!doc){lyricStatus="歌词服务请求失败";return;}list=lyricCandidates(doc);
 }
 int count=cJSON_GetArraySize(list);Serial.printf("LRC candidates: %d\n",count);
 String ids[3],keys[3];int tries=min(3,count);
 for(int i=0;i<tries;i++){cJSON* candidate=cJSON_GetArrayItem(list,i);ids[i]=jsonText(candidate,"id");keys[i]=jsonText(candidate,"accesskey");}
 cJSON_Delete(doc);
 if(!tries){lyricStatus="未找到匹配歌词";return;}
 for(int i=0;i<tries;i++){
  if(!ids[i].length()||!keys[i].length())continue;
  doc=lyricRequest("/download",{{"ver","1"},{"client","pc"},{"id",ids[i]},{"accesskey",keys[i]},{"fmt","lrc"},{"charset","utf8"}});
  if(!doc)continue;
  String content=jsonText(doc,"content");if(!content.length())content=jsonText(cJSON_GetObjectItemCaseSensitive(doc,"data"),"content");cJSON_Delete(doc);
  if(!content.length()||content.length()>49152)continue;
  if(content.startsWith("["))parseLyrics(content);
  else {size_t size=0;mbedtls_base64_decode(nullptr,0,&size,(const unsigned char*)content.c_str(),content.length());
   if(size&&size<=32768){unsigned char* buffer=(unsigned char*)malloc(size+1);if(buffer){
    if(mbedtls_base64_decode(buffer,size,&size,(const unsigned char*)content.c_str(),content.length())==0){buffer[size]=0;parseLyrics(String((const char*)buffer));}free(buffer);
   }}
  }
  if(lyricCount){lyricCacheHash=cache;lyricSelected=-2;lyricStatus="";return;}
 }
 lyricStatus="暂无可用的 LRC 歌词";
}
static void prepareNetworkLyrics(int index){if(index>=0&&index<musicCount)fetchNetworkLyrics(musicResults[index].hash,musicResults[index].audioId,musicResults[index].title,musicResults[index].durationMs);}
static void lyricProgress() {
 int current=lyricCount?lrcAt(lyricLines,lyricCount,trackPositionMs):-1;
 if(current==lyricSelected)return;
 lyricSelected=current;
 String text=current>=0?String(lyricLines[current].text):(lyricCount?String("等待歌词…"):lyricStatus);
 String frame="LY:";
 for(size_t i=0;i<text.length();i++){char h[3];snprintf(h,sizeof(h),"%02X",(unsigned)(uint8_t)text[i]);frame+=h;}
 uiFrame(frame);
}
