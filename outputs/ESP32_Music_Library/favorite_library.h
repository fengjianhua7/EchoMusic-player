#pragma once
// Read-only cloud library, adapted from KuGouMusicApi user_playlist and
// playlist_track_all_new. Account changes invalidate the in-memory cache.
static MusicResult favoriteSongs[5];
static int favoriteCount=0,favoritePage=0,favoriteRequestPage=0,favoritePlay=-1;
static bool favoriteHasNext=false;
static uint32_t favoriteRevision=0;
static String favoriteStatus="登录后读取酷狗「我喜欢」",favoriteOwner,favoriteListId,favoriteGlobalId;
static int favoriteListType=0;
static String favoriteField(cJSON* obj,const char* a,const char* b=nullptr,const char* c=nullptr) {
 String s=jsonText(obj,a);if(!s.length()&&b)s=jsonText(obj,b);if(!s.length()&&c)s=jsonText(obj,c);return s;
}
static void invalidateFavorites() {
 favoriteCount=0;favoritePage=0;favoriteHasNext=false;favoriteListId="";favoriteGlobalId="";
 favoriteRequestPage=0;favoritePlay=-1;favoriteRevision++;
 favoriteStatus="登录后读取酷狗「我喜欢」";
}
static void checkFavoriteAccount() {
 String owner=md5Text(sessionUser+":"+sessionToken);
 if(owner!=favoriteOwner){favoriteOwner=owner;invalidateFavorites();}
}
static bool findFavoriteList(cJSON* node,int depth=0) {
 if(!node||depth>6)return false;
 if(cJSON_IsObject(node)) {
  String name=favoriteField(node,"name","listname","list_name");
  if(name=="我喜欢"||name=="我喜欢的音乐"||name=="默认收藏"||name=="喜欢的音乐") {
   String id=favoriteField(node,"listid","list_id");
   String global=favoriteField(node,"global_collection_id","globalCollectionId");
   if(id.length()||global.length()) {
    favoriteListId=id;favoriteGlobalId=global;
    String type=jsonText(node,"type");favoriteListType=type.length()?type.toInt():0;return true;
   }
  }
 }
 for(cJSON* child=node->child;child;child=child->next)
  if((cJSON_IsObject(child)||cJSON_IsArray(child))&&findFavoriteList(child,depth+1))return true;
 return false;
}
static String favoritePostBody(int page,bool songs) {
 cJSON* body=cJSON_CreateObject();if(!body)return "";
 // User IDs are numeric in the upstream app request.
 cJSON_AddNumberToObject(body,"userid",strtod(sessionUser.c_str(),nullptr));
 cJSON_AddStringToObject(body,"token",sessionToken.c_str());
 cJSON_AddNumberToObject(body,"page",page);cJSON_AddNumberToObject(body,"pagesize",songs?5:30);
 cJSON_AddNumberToObject(body,"type",songs?favoriteListType:2);
 if(songs){cJSON_AddNumberToObject(body,"listid",strtod(favoriteListId.c_str(),nullptr));cJSON_AddNumberToObject(body,"area_code",1);cJSON_AddNumberToObject(body,"allplatform",1);cJSON_AddNumberToObject(body,"show_cover",1);}
 else cJSON_AddNumberToObject(body,"total_ver",979);
 char* text=cJSON_PrintUnformatted(body);String result=text?String(text):String();
 if(text)cJSON_free(text);cJSON_Delete(body);return result;
}
static bool discoverFavorites() {
 for(int page=1;page<=10;page++) {
  String body=favoritePostBody(page,false);if(!body.length())return false;
  cJSON* doc=apiGet("/v7/get_all_list","cloudlist.service.kugou.com",{{"plat","1"}},false,body);
  if(!doc){favoriteStatus="歌单读取失败，请检查登录及串口日志";return false;}
  bool found=findFavoriteList(cJSON_GetObjectItemCaseSensitive(doc,"data"));
  cJSON_Delete(doc);if(found)return true;
 }
 favoriteStatus="未识别到「我喜欢」歌单，请在酷狗 APP 检查是否已同步";return false;
}
static cJSON* favoriteTrackArray(cJSON* doc) {
 cJSON* data=cJSON_GetObjectItemCaseSensitive(doc,"data");
 if(cJSON_IsArray(data))return data;
 const char* names[]={"info","songs","list","lists","file_list"};
 for(const char* name:names){cJSON* a=cJSON_GetObjectItemCaseSensitive(data,name);if(cJSON_IsArray(a))return a;}
 return nullptr;
}
static void loadFavorites(int page) {
 if(!sessionToken.length()||sessionUser=="0"||!sessionUser.length()){favoriteStatus="请先扫码登录酷狗账号";return;}
 favoriteStatus="正在读取酷狗「我喜欢」…";
 if(!favoriteListId.length()&&!favoriteGlobalId.length()&&!discoverFavorites())return;
 cJSON* doc=nullptr;
 if(favoriteListId.length()) {
  String body=favoritePostBody(page,true);if(!body.length()){favoriteStatus="内存不足，请稍后重试";return;}
  doc=apiGet("/v4/get_list_all_file_v3","cloudlist.service.kugou.com",{},false,body);
 } else {
  doc=apiGet("/pubsongs/v2/get_other_list_file_nofilt",nullptr,
   {{"area_code","1"},{"begin_idx",String((page-1)*5)},{"plat","1"},{"type","1"},{"mode","1"},
    {"personal_switch","1"},{"pagesize","5"},{"global_collection_id",favoriteGlobalId}});
 }
 if(!doc){favoriteStatus="喜欢列表请求失败，已保留上一页";return;}
 cJSON* list=favoriteTrackArray(doc);
 if(!list){favoriteStatus="歌单响应格式未识别，已保留上一页";Serial.println("Favorites: unexpected song list schema");cJSON_Delete(doc);return;}
 MusicResult next[5];int count=0,excluded=0;
 cJSON* item;
 cJSON_ArrayForEach(item,list) {
  if(count>=5)break;
  String pay=favoriteField(item,"PayType","pay_type","paytype");
  if(pay=="2"){excluded++;continue;}
  MusicResult song;
  song.hash=favoriteField(item,"hash","FileHash","filehash");
  if(!song.hash.length())continue;
  song.title=favoriteField(item,"filename","FileName","name");
  if(!song.title.length())song.title=favoriteField(item,"songname","song_name");
  song.albumId=favoriteField(item,"album_id","AlbumID","albumid");
  song.audioId=favoriteField(item,"album_audio_id","mixsongid","MixSongID");
  if(!song.audioId.length())song.audioId=jsonText(item,"AlbumAudioID");
  if(!song.title.length())song.title="未命名歌曲";
  next[count++]=song;
 }
 int rawCount=cJSON_GetArraySize(list);
 if(rawCount>0&&!count&&excluded<rawCount){
  favoriteStatus="歌单曲目字段未识别，已保留上一页";Serial.println("Favorites: no usable hash fields");cJSON_Delete(doc);return;
 }
 cJSON* data=cJSON_GetObjectItemCaseSensitive(doc,"data");
 String total=favoriteField(data,"count","total","total_count");
 favoriteHasNext=total.length()?((page*5)<total.toInt()):(rawCount>=5);
 cJSON_Delete(doc);
 for(int i=0;i<5;i++)favoriteSongs[i]=next[i];
 favoriteCount=count;favoritePage=page;favoriteRevision++;
 favoriteStatus="第 "+String(page)+" 页 · "+String(count)+" 首";
 if(excluded)favoriteStatus+=" · 已排除 "+String(excluded)+" 首付费购买版本";
 if(!rawCount)favoriteStatus="这一页没有歌曲";
 Serial.printf("Favorites loaded: page=%d songs=%d excluded=%d\n",page,count,excluded);
}
static void processFavorites() {
 checkFavoriteAccount();
 if(playbackActive||saveMode)return;
 if(favoriteRequestPage){int page=favoriteRequestPage;favoriteRequestPage=0;stopCardLocal();loadFavorites(page);}
 if(favoritePlay>=0){int index=favoritePlay;favoritePlay=-1;if(index>=favoriteCount)return;
  stopCardLocal();musicCount=favoriteCount;
  for(int i=0;i<favoriteCount;i++)musicResults[i]=favoriteSongs[i];
  syncSongList();boardCommand='1'+index;
 }
}
