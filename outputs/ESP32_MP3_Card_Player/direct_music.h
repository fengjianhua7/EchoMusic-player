static void syncSongList();
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
#include "login_root.h"
#include "esp_heap_caps.h"

static String musicMid;
static String sessionUser,sessionToken;
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
  sessionUser=prefs.getString("userid",MUSIC_USER_ID);
  sessionToken=prefs.getString("token",MUSIC_TOKEN);
  prefs.end();
}
static String jsonText(cJSON* obj,const char* key) {
  cJSON* v=cJSON_GetObjectItemCaseSensitive(obj,key);
  if(cJSON_IsString(v)) return String(v->valuestring);
  if(cJSON_IsNumber(v)) {char b[40];snprintf(b,sizeof(b),"%.0f",v->valuedouble);return String(b);}
  return "";
}
static cJSON* apiGet(const char* path,const char* router,std::map<String,String> p, bool loginHost=false) {
  if(time(nullptr)<1700000000) {Serial.println("Clock not synchronized");return nullptr;}
  if(!p.count("dfid"))p["dfid"]="-";p["mid"]=musicMid;p["uuid"]="-";
  if(!p.count("appid")) p["appid"]="3116";
  if(!p.count("clientver")) p["clientver"]="11440";
  p["clienttime"]=String((unsigned long)time(nullptr));
  if(sessionToken.length()) p["token"]=sessionToken;
  if(sessionUser.length()&&sessionUser!="0") p["userid"]=sessionUser;
  const String salt=loginHost?"NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt":"LnT6xpN3khm36zse0QzvmgTZ3waWdRSA";
  String canonical=salt;
  for(const auto& item:p) canonical+=item.first+"="+item.second;
  canonical+=salt; p["signature"]=md5Text(canonical);
  const char* host=loginHost?"login-user.kugou.com":"gateway.kugou.com";
  String url=String("https://")+host+path+"?";
  for(const auto& item:p) url+=urlEncode(item.first)+"="+urlEncode(item.second)+"&";
  IPAddress gatewayIp;
  if(!WiFi.hostByName(host,gatewayIp)) {
    Serial.println("DNS failed: gateway.kugou.com");return nullptr;
  }
  Serial.print("Gateway IP: ");Serial.println(gatewayIp);
  Serial.printf("TLS heap: free=%u largest=%u, epoch=%lu\n",ESP.getFreeHeap(),
    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned long)time(nullptr));
  NetworkClientSecure tls; tls.setCACert(loginHost?LOGIN_ROOT_CA:GATEWAY_ROOT_CA);
  tls.setHandshakeTimeout(20);
  HTTPClient http;
  http.setTimeout(20000);
  http.setConnectTimeout(20000);
  if(!http.begin(tls,url)) return nullptr;
  if(router && router[0])http.addHeader("x-router",router);
  http.addHeader("dfid",p["dfid"]);http.addHeader("mid",musicMid);
  http.addHeader("clienttime",p["clienttime"]);
  http.addHeader("kg-rc","1");http.addHeader("kg-thash","5d816a0");
  http.addHeader("kg-rec","1");http.addHeader("kg-rf","B9EDA08A64250DEFFBCADDEE00F8F25F");
  const char* headers[]={"ssa-code","Content-Type","Content-Encoding","Transfer-Encoding"};http.collectHeaders(headers,4);
  http.addHeader("Accept-Encoding","identity");
  int status=http.GET();
  Serial.printf("API %s: HTTP %d\n",path,status);
  if(status!=200) {
    Serial.print("HTTP detail: ");Serial.println(HTTPClient::errorToString(status));
    if(status<0) {
    char errorText[192]={0};
    int tlsCode=tls.lastError(errorText,sizeof(errorText));
    Serial.printf("TLS detail: %d %s\n",tlsCode,errorText);
    }
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
  if(!error.length())error=jsonText(doc,"errcode");
  if(ok=="0" || (error.length()&&error!="0")) {
    Serial.print("Platform error: ");Serial.print(error);Serial.print(" ");
    String message=jsonText(doc,"msg");
    if(!message.length())message=jsonText(doc,"error");
    Serial.println(message);cJSON_Delete(doc);return nullptr;
  }
  return doc;
}
struct MusicResult {String title,hash,albumId,audioId;};
static MusicResult musicResults[5];
static int musicCount=0;
// Exclude only explicitly purchase-required base versions; retain VIP/unknown.
static bool includeSearchVersion(cJSON* item) {
  String payType=jsonText(item,"PayType");
  return payType!="2";
}
static void searchMusic(const String& keyword) {
  MusicResult previous[5];int previousCount=musicCount;
  for(int i=0;i<previousCount;i++)previous[i]=musicResults[i];
  musicCount=0;
  unsigned skipped=0,scanned=0;
  bool interrupted=false;
  for(int page=1;page<=10 && musicCount<5;page++) {
    std::map<String,String> p={{"keyword",keyword},{"page",String(page)},{"pagesize","2"},
      {"platform","AndroidFilter"},{"clientver","201"},{"iscorrection","1"},
      {"privilegefilter","0"},{"area_code","1"},{"dopicfull","0"}};
    cJSON* doc=apiGet("/v2/search/song","complexsearch.kugou.com",p);
    if(!doc){interrupted=true;break;}
    cJSON* data=cJSON_GetObjectItemCaseSensitive(doc,"data");
    cJSON* list=cJSON_GetObjectItemCaseSensitive(data,"lists");
    if(!cJSON_IsArray(list))list=cJSON_GetObjectItemCaseSensitive(data,"info");
    if(!cJSON_IsArray(list)) {
      Serial.println("Search response schema changed");cJSON_Delete(doc);interrupted=true;break;
    }
    int count=cJSON_GetArraySize(list);
    cJSON* item;
    cJSON_ArrayForEach(item,list) {
      if(musicCount==5)break;
      scanned++;
      if(!includeSearchVersion(item)){skipped++;continue;}
      MusicResult r;
      r.hash=jsonText(item,"FileHash");if(!r.hash.length())r.hash=jsonText(item,"hash");
      if(!r.hash.length())continue;
      bool duplicate=false;
      for(int i=0;i<musicCount;i++)if(r.hash.equalsIgnoreCase(musicResults[i].hash))duplicate=true;
      if(duplicate)continue;
      r.title=jsonText(item,"FileName");if(!r.title.length())r.title=jsonText(item,"filename");
      r.albumId=jsonText(item,"AlbumID");if(!r.albumId.length())r.albumId=jsonText(item,"album_id");
      r.audioId=jsonText(item,"AlbumAudioID");if(!r.audioId.length())r.audioId=jsonText(item,"album_audio_id");
      if(!r.audioId.length())r.audioId=jsonText(item,"MixSongID");
      if(!r.audioId.length())r.audioId=jsonText(item,"mixsongid");
      musicResults[musicCount]=r;
      Serial.printf("%d: %s\n",musicCount+1,r.title.c_str());musicCount++;
    }
    cJSON_Delete(doc);
    if(count<2)break;
  }
  Serial.printf("Purchase-only filter: scanned=%u, excluded=%u, listed=%d%s\n",scanned,skipped,musicCount,interrupted?" (search interrupted)":"");
  if(interrupted && !musicCount) {
    for(int i=0;i<previousCount;i++)musicResults[i]=previous[i];
    musicCount=previousCount;
    Serial.println("Search failed; previous list retained");
  } else if(!musicCount)Serial.println("No matching non-purchase versions found in first 20 candidates");
}
static String resolveMusic(int index) {
  if(index<0||index>=musicCount)return "";
  MusicResult r=musicResults[index];r.hash.toLowerCase();
  cJSON* doc=apiGet("/v1/user_verify","trackercdngz.kugou.com",{{"module_id","51"}});
  if(!doc)return "";
  String auth=jsonText(cJSON_GetObjectItemCaseSensitive(doc,"data"),"auth");
  Serial.print("User verify business status: ");Serial.println(jsonText(doc,"status"));
  Serial.printf("User auth length: %u\n",(unsigned)auth.length());
  cJSON_Delete(doc);
  if(!auth.length()){Serial.println("User authorization unavailable; login may be required");return "";}
  String aid=r.audioId.length()?r.audioId:"0";
  Serial.print("Song album_audio_id: ");Serial.println(aid);
  doc=apiGet("/v1/authorization","trackercdngz.kugou.com",{{"authorization",auth},{"module_id","51"},
    {"album_audio_id",aid},{"clientver","11561"},{"hash",r.hash}});
  if(!doc)return "";
  cJSON* data=cJSON_GetObjectItemCaseSensitive(doc,"data");
  auth=jsonText(data,"auth");String openTime=jsonText(data,"open_time");
  Serial.print("Song authorization business status: ");Serial.println(jsonText(doc,"status"));
  Serial.printf("Song auth length=%u, open_time present=%s\n",(unsigned)auth.length(),openTime.length()?"yes":"no");
  cJSON_Delete(doc);
  if(!auth.length()){Serial.println("Song authorization unavailable");return "";}
  std::map<String,String> p={{"album_id",r.albumId.length()?r.albumId:"0"},{"area_code","1"},{"module",""},
   {"hash",r.hash},{"need_m","0"},{"ssa_flag","is_fromtrack"},{"version","11430"},
   {"open_time",openTime.length()?openTime:"0"},{"ptype","0"},{"need_ogg","0"},{"page_id","967177915"},
   {"auth",auth},{"mtype","0"},{"quality","128"},{"album_audio_id",aid},{"behavior","play"},
   {"pid","411"},{"module_id","51"},{"cmd","26"},{"ppage_id","356753938,823673182,967485191"},
   {"clientver","11561"},{"pidversion","3001"},{"cdnBackup","1"}};
  p["key"]=md5Text(r.hash+"185672dd44712f60bb1736df5a377e82"+"3116"+musicMid+(sessionUser.length()?sessionUser:String("0")));
  p["dfid"]=md5Text(String(esp_random())+String(esp_random())).substring(0,24);
  doc=apiGet("/tracker/v5/url","",p);
  if(!doc)return "";
  cJSON* urls=cJSON_GetObjectItemCaseSensitive(doc,"url");
  if(!urls)urls=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(doc,"data"),"url");
  String url;
  if(cJSON_IsString(urls))url=urls->valuestring;
  else if(cJSON_IsArray(urls)){cJSON* first=cJSON_GetArrayItem(urls,0);if(cJSON_IsString(first))url=first->valuestring;}
  if(!url.length()) {
    Serial.println("No playback URL; response structure:");
    cJSON* field;
    cJSON_ArrayForEach(field,doc) {
      Serial.print("  ");Serial.print(field->string?field->string:"?");
      if(cJSON_IsNumber(field)) Serial.printf(" = %.0f\n",field->valuedouble);
      else if(cJSON_IsString(field)) Serial.printf(" : string(length=%u)\n",(unsigned)strlen(field->valuestring));
      else if(cJSON_IsArray(field)) Serial.printf(" : array(count=%d)\n",cJSON_GetArraySize(field));
      else if(cJSON_IsObject(field)) {
        Serial.println(" : object");
        cJSON* child;
        cJSON_ArrayForEach(child,field) {
          Serial.print("    ");Serial.print(child->string?child->string:"?");
          if(cJSON_IsNumber(child))Serial.printf(" = %.0f\n",child->valuedouble);
          else Serial.printf(" : type=%d\n",child->type);
        }
      } else Serial.printf(" : type=%d\n",field->type);
    }
  }
  cJSON_Delete(doc);
  return url;
}
