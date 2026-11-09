#pragma once
#include "lrc_core.h"
#include "mbedtls/base64.h"
static LrcEntry* lyricLines=nullptr;
static unsigned lyricCount=0;
static int lyricSelected=-2;
static String localLyricText,lyricCacheHash;
static void clearLyrics(){lyricCount=0;lyricSelected=-2;localLyricText="";}
static void parseLyrics(const String& text) {
 if(!lyricLines){lyricLines=(LrcEntry*)ps_malloc(sizeof(LrcEntry)*256);if(!lyricLines)lyricLines=(LrcEntry*)malloc(sizeof(LrcEntry)*256);}
 lyricCount=lyricLines?lrcParse(text.c_str(),lyricLines,256):0;lyricSelected=-2;
 Serial.printf("LRC lines: %u\n",lyricCount);
}
static void receiveLocalLyric(const String& hex) {
 if(localLyricText.length()+hex.length()/2>32768)return;
 for(size_t i=0;i+1<hex.length();i+=2){char h[3]={hex[i],hex[i+1],0};localLyricText+=(char)strtoul(h,nullptr,16);}
}
static void prepareLocalLyrics(int index) {
 clearLyrics();lyricCacheHash="";
 if(cardCommand("LC:"+String(index),"SD_LRC_END",15000)&&localLyricText.length())parseLyrics(localLyricText);
 localLyricText="";
}
// Public lyric service: no account token, cookie or device session is sent.
static cJSON* lyricRequest(const String& path,std::map<String,String> params) {
 String url="https://lyrics.kugou.com"+path+"?";
 for(const auto& p:params)url+=urlEncode(p.first)+"="+urlEncode(p.second)+"&";
 NetworkClientSecure tls;tls.setCACert(MUSIC_ROOT_CA);tls.setHandshakeTimeout(8);
 HTTPClient http;http.setTimeout(8000);http.setConnectTimeout(8000);http.useHTTP10(true);
 if(!http.begin(tls,url))return nullptr;
 http.addHeader("Accept-Encoding","identity");int code=http.GET();
 if(code!=200||http.getSize()>65536){Serial.printf("LRC request HTTP %d\n",code);http.end();return nullptr;}
 String text=http.getString();http.end();if(text.length()>65536)return nullptr;
 return cJSON_Parse(text.c_str());
}
static void prepareNetworkLyrics(int index) {
 if(index<0||index>=musicCount)return;
 MusicResult& song=musicResults[index];
 if(lyricCacheHash==song.hash){lyricSelected=-2;return;}
 clearLyrics();lyricCacheHash=song.hash;
 cJSON* doc=lyricRequest("/v1/search",{{"ver","1"},{"man","no"},{"client","pc"},{"lrctxt","1"},
  {"hash",song.hash},{"album_audio_id",song.audioId},{"duration",String(song.durationMs)},{"keyword",song.title}});
 if(!doc)return;
 cJSON* list=cJSON_GetObjectItemCaseSensitive(doc,"candidates");
 if(!cJSON_IsArray(list))list=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(doc,"data"),"candidates");
 cJSON* first=cJSON_GetArrayItem(list,0);
 String id=jsonText(first,"id"),key=jsonText(first,"accesskey");cJSON_Delete(doc);
 if(!id.length()||!key.length())return;
 doc=lyricRequest("/download",{{"ver","1"},{"client","pc"},{"id",id},{"accesskey",key},{"fmt","lrc"},{"charset","utf8"}});
 if(!doc)return;
 String content=jsonText(doc,"content");cJSON_Delete(doc);
 if(!content.length()||content.length()>49152)return;
 size_t size=0;
 mbedtls_base64_decode(nullptr,0,&size,(const unsigned char*)content.c_str(),content.length());
 if(!size||size>32768)return;
 unsigned char* buffer=(unsigned char*)malloc(size+1);if(!buffer)return;
 if(mbedtls_base64_decode(buffer,size,&size,(const unsigned char*)content.c_str(),content.length())==0){buffer[size]=0;parseLyrics(String((const char*)buffer));}
 free(buffer);
}
static void lyricProgress() {
 int current=lyricCount?lrcAt(lyricLines,lyricCount,trackPositionMs):-1;
 if(current==lyricSelected)return;
 lyricSelected=current;
 String text=current>=0?String(lyricLines[current].text):(lyricCount?String("等待歌词…"):String("暂无同步歌词"));
 String frame="LY:";
 for(size_t i=0;i<text.length();i++){char h[3];snprintf(h,sizeof(h),"%02X",(unsigned)(uint8_t)text[i]);frame+=h;}
 uiFrame(frame);
}
