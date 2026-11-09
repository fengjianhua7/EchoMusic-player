// API adaptation derived from MakcRe/KuGouMusicApi. See LICENSE.KuGouMusicApi.
#pragma once
#include <map>
#include <MD5Builder.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <time.h>
#include "cJSON.h"
#include "trusted_roots.h"
#include "gateway_root.h"
#include "esp_heap_caps.h"

static String musicMid;
static String md5Text(const String& text) {
  MD5Builder m; m.begin(); m.add(text); m.calculate(); return m.toString();
}
static String urlEncode(const String& text) {
  String out;
  const char* hex="0123456789ABCDEF";
  for(size_t i=0;i<text.length();i++) {
    uint8_t c=(uint8_t)text[i];
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~') out+=(char)c;
    else {out+='%';out+=hex[c>>4];out+=hex[c&15];}
  }
  return out;
}
static void initMusicIdentity() {
  Preferences prefs; prefs.begin("directmusic",false);
  musicMid=prefs.getString("mid","");
  if(!musicMid.length()) {
    musicMid=md5Text(WiFi.macAddress());
    prefs.putString("mid",musicMid);
  }
  prefs.end();
}
static String jsonText(cJSON* obj,const char* key) {
  cJSON* v=cJSON_GetObjectItemCaseSensitive(obj,key);
  if(cJSON_IsString(v)) return String(v->valuestring);
  if(cJSON_IsNumber(v)) {char b[40];snprintf(b,sizeof(b),"%.0f",v->valuedouble);return String(b);}
  return "";
}
static cJSON* apiGet(const char* path,const char* router,std::map<String,String> p) {
  if(time(nullptr)<1700000000) {Serial.println("Clock not synchronized");return nullptr;}
  p["dfid"]="-";p["mid"]=musicMid;p["uuid"]="-";
  if(!p.count("appid")) p["appid"]="1005";
  if(!p.count("clientver")) p["clientver"]="20489";
  p["clienttime"]=String((unsigned long)time(nullptr));
  if(strlen(MUSIC_TOKEN)) p["token"]=MUSIC_TOKEN;
  if(strlen(MUSIC_USER_ID)&&String(MUSIC_USER_ID)!="0") p["userid"]=MUSIC_USER_ID;
  const String salt="OIlwieks28dk2k092lksi2UIkp";
  String canonical=salt;
  for(const auto& item:p) canonical+=item.first+"="+item.second;
  canonical+=salt; p["signature"]=md5Text(canonical);
  String url=String("https://gateway.kugou.com")+path+"?";
  for(const auto& item:p) url+=urlEncode(item.first)+"="+urlEncode(item.second)+"&";
  IPAddress gatewayIp;
  if(!WiFi.hostByName("gateway.kugou.com",gatewayIp)) {
    Serial.println("DNS failed: gateway.kugou.com");return nullptr;
  }
  Serial.print("Gateway IP: ");Serial.println(gatewayIp);
  Serial.printf("TLS heap: free=%u largest=%u, epoch=%lu\n",ESP.getFreeHeap(),
    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned long)time(nullptr));
  NetworkClientSecure tls; tls.setCACert(GATEWAY_ROOT_CA);
  tls.setHandshakeTimeout(20);
  HTTPClient http;
  http.setTimeout(20000);
  http.setConnectTimeout(20000);
  if(!http.begin(tls,url)) return nullptr;
  http.addHeader("x-router",router);
  http.addHeader("dfid","-");http.addHeader("mid",musicMid);
  http.addHeader("clienttime",p["clienttime"]);
  http.addHeader("kg-rc","1");http.addHeader("kg-thash","5d816a0");
  http.addHeader("kg-rec","1");http.addHeader("kg-rf","B9EDA08A64250DEFFBCADDEE00F8F25F");
  const char* headers[]={"ssa-code","Content-Type","Content-Encoding","Transfer-Encoding"};http.collectHeaders(headers,4);
  http.addHeader("Accept-Encoding","identity");
  int status=http.GET();
  Serial.printf("API %s: HTTP %d\n",path,status);
  if(status!=200) {
    Serial.print("HTTP detail: ");Serial.println(HTTPClient::errorToString(status));
    char errorText[192]={0};
    int tlsCode=tls.lastError(errorText,sizeof(errorText));
    Serial.printf("TLS detail: %d %s\n",tlsCode,errorText);
    http.end();return nullptr;
  }
  if(http.header("ssa-code").length()) {
    Serial.println("Platform verification required. Automatic playback stopped.");http.end();return nullptr;
  }
  if(http.getSize()>196608) {Serial.println("API response too large");http.end();return nullptr;}
  int expected=http.getSize();
  String encoding=http.header("Content-Encoding");
  String mime=http.header("Content-Type");
  String body=http.getString();
  http.end();tls.stop();
  Serial.printf("JSON response: expected=%d received=%u free=%u\n",expected,(unsigned)body.length(),ESP.getFreeHeap());
  Serial.print("Content-Type: ");Serial.println(mime);
  Serial.print("Content-Encoding: ");Serial.println(encoding);
  if(expected>=0 && body.length()!=(size_t)expected) {
    Serial.println("Response incomplete; retry search");return nullptr;
  }
  if(encoding.length() && encoding!="identity") {
    Serial.println("Compressed JSON response unsupported");return nullptr;
  }
  cJSON* doc=cJSON_Parse(body.c_str());
  if(!doc) {
    const char* error=cJSON_GetErrorPtr();
    long offset=error?(long)(error-body.c_str()):-1;
    Serial.printf("JSON parse failed: offset=%ld, free=%u\n",offset,ESP.getFreeHeap());
    Serial.print("First bytes (hex): ");
    for(size_t i=0;i<body.length() && i<16;i++)Serial.printf("%02X ",(unsigned)(uint8_t)body[i]);
    Serial.println();return nullptr;
  }
  String ok=jsonText(doc,"status"), error=jsonText(doc,"error_code");
  if(ok=="0" || (error.length()&&error!="0")) {
    Serial.print("Platform error: ");Serial.print(error);Serial.print(" ");
    Serial.println(jsonText(doc,"msg"));cJSON_Delete(doc);return nullptr;
  }
  return doc;
}
struct MusicResult {String title,hash,albumId,audioId;};
static MusicResult musicResults[5];
static int musicCount=0;
static void searchMusic(const String& keyword) {
  musicCount=0;
  std::map<String,String> p={{"keyword",keyword},{"page","1"},{"pagesize","1"},
    {"platform","AndroidFilter"},{"iscorrection","1"},{"privilegefilter","0"},{"area_code","1"},{"dopicfull","0"}};
  cJSON* doc=apiGet("/v2/search/song","complexsearch.kugou.com",p);
  if(!doc)return;
  cJSON* data=cJSON_GetObjectItemCaseSensitive(doc,"data");
  cJSON* list=cJSON_GetObjectItemCaseSensitive(data,"lists");
  if(!cJSON_IsArray(list))list=cJSON_GetObjectItemCaseSensitive(data,"info");
  cJSON* item;
  cJSON_ArrayForEach(item,list) {
    if(musicCount==5)break;
    MusicResult r;
    r.hash=jsonText(item,"FileHash");if(!r.hash.length())r.hash=jsonText(item,"hash");
    if(!r.hash.length())continue;
    r.title=jsonText(item,"FileName");if(!r.title.length())r.title=jsonText(item,"filename");
    r.albumId=jsonText(item,"AlbumID");if(!r.albumId.length())r.albumId=jsonText(item,"album_id");
    r.audioId=jsonText(item,"AlbumAudioID");if(!r.audioId.length())r.audioId=jsonText(item,"album_audio_id");
    musicResults[musicCount]=r;
    Serial.printf("%d: %s\n",musicCount+1,r.title.c_str());musicCount++;
  }
  if(!musicCount)Serial.println("No usable search results; response schema may have changed");
  cJSON_Delete(doc);
}
static String resolveMusic(int index) {
  if(index<0||index>=musicCount)return "";
  MusicResult r=musicResults[index];r.hash.toLowerCase();
  cJSON* doc=apiGet("/v1/user_verify","trackercdngz.kugou.com",{{"module_id","51"}});
  if(!doc)return "";
  String auth=jsonText(cJSON_GetObjectItemCaseSensitive(doc,"data"),"auth");cJSON_Delete(doc);
  if(!auth.length()){Serial.println("User authorization unavailable; login may be required");return "";}
  String aid=r.audioId.length()?r.audioId:"0";
  doc=apiGet("/v1/authorization","trackercdngz.kugou.com",{{"authorization",auth},{"module_id","51"},
    {"album_audio_id",aid},{"clientver","11561"},{"hash",r.hash}});
  if(!doc)return "";
  cJSON* data=cJSON_GetObjectItemCaseSensitive(doc,"data");
  auth=jsonText(data,"auth");String openTime=jsonText(data,"open_time");cJSON_Delete(doc);
  if(!auth.length()){Serial.println("Song authorization unavailable");return "";}
  std::map<String,String> p={{"album_id",r.albumId.length()?r.albumId:"0"},{"area_code","1"},{"module",""},
   {"hash",r.hash},{"need_m","0"},{"ssa_flag","is_fromtrack"},{"version","11430"},
   {"open_time",openTime.length()?openTime:"0"},{"ptype","0"},{"need_ogg","1"},{"page_id","151369488"},
   {"auth",auth},{"mtype","0"},{"quality","128"},{"album_audio_id",aid},{"behavior","play"},
   {"pid","2"},{"module_id","51"},{"cmd","26"},{"ppage_id","463467626,350369493,788954147"},
   {"clientver","11561"},{"pidversion","3001"},{"cdnBackup","1"}};
  p["key"]=md5Text(r.hash+"57ae12eb6890223e355ccfcb74edf70d"+"1005"+musicMid+String(strlen(MUSIC_USER_ID)?MUSIC_USER_ID:"0"));
  doc=apiGet("/tracker/v5/url","trackercdngz.kugou.com",p);
  if(!doc)return "";
  cJSON* urls=cJSON_GetObjectItemCaseSensitive(doc,"url");
  if(!urls)urls=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(doc,"data"),"url");
  String url;
  if(cJSON_IsString(urls))url=urls->valuestring;
  else if(cJSON_IsArray(urls)){cJSON* first=cJSON_GetArrayItem(urls,0);if(cJSON_IsString(first))url=first->valuestring;}
  cJSON_Delete(doc);
  if(!url.length())Serial.println("No playback URL returned");
  return url;
}
