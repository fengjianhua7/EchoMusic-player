#pragma once
#include <WebServer.h>
#include <ESPmDNS.h>
static WebServer phoneServer(80);
static bool phoneStarted=false;
static String pendingSearch;
static bool searchQueued=false;
static const char PHONE_HTML[] PROGMEM=R"PAGE(<!doctype html>
<html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>音乐播放器</title><style>
*{box-sizing:border-box}body{margin:0;background:#f2f5f8;color:#142333;font:16px system-ui,sans-serif}main{max-width:680px;margin:auto;padding:24px 18px}h1{font-size:28px;margin:8px 0}p{color:#52677b;line-height:1.6}form{display:flex;gap:8px;margin:24px 0 16px}input{min-width:0;flex:1;border:1px solid #b8c6d3;border-radius:12px;padding:14px;font-size:16px}button{border:0;border-radius:12px;padding:13px 17px;background:#176b59;color:white;font-size:16px;cursor:pointer}button:disabled{opacity:.5}#songs{display:grid;gap:10px;margin:20px 0}.song{display:flex;align-items:center;gap:12px;background:white;border:1px solid #dee6ec;border-radius:14px;padding:14px}.song span{flex:1;line-height:1.5}.song.active{border:2px solid #176b59}.song button{padding:9px 12px}.controls{display:flex;flex-wrap:wrap;gap:8px}.controls button{flex:1;background:#203c53;white-space:nowrap}.card{background:white;border-radius:16px;padding:18px;margin-top:18px}#status{min-height:26px;font-weight:600;color:#176b59}#notice{min-height:25px;color:#52677b}small{color:#52677b}header small{letter-spacing:.1em}label{display:block;margin-bottom:10px}.volume{display:flex;gap:12px;align-items:center}.volume button{background:#203c53}#vol{flex:1;text-align:center;font-size:24px;font-weight:650}</style>
<main><header><small>STM32 · ESP32-S3</small><h1>音乐播放器</h1><p>搜索歌曲，选择版本，开始播放。</p></header>
<form id="search"><input id="query" aria-label="歌曲名或歌手" placeholder="输入歌曲名或歌手" maxlength="60" required><button>搜索</button></form>
<div id="status" role="status">正在连接播放器…</div><div id="notice" role="status"></div><div id="songs"></div>
<div class="controls"><button id="toggle" data-action="toggle">播放</button><button data-action="next">下一首</button><button data-action="stop">停止</button></div>
<div class="card"><label for="mode">播放模式</label><select id="mode" style="width:100%;padding:12px;font-size:16px"><option value="0">单曲播放</option><option value="1">列表循环</option><option value="2">单曲循环</option><option value="3">随机播放</option></select></div><div class="card"><label>音量</label><div class="volume"><button data-action="down" aria-label="降低音量">−</button><div id="vol">25%</div><button data-action="up" aria-label="增加音量">＋</button></div></div>
<div class="card"><button data-action="login">扫码登录</button><p>登录二维码会显示在开发板屏幕，请用酷狗 APP 扫描。</p><small>列表仅排除付费购买版本，VIP 歌曲仍会保留。</small></div></main>
<script>
const el=id=>document.getElementById(id);let locked=false,lastSongs='',revision=0;
async function post(path,body){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(body)});if(!r.ok)throw Error(await r.text());el('notice').textContent='操作已接收';}
async function action(a){try{await post('/api/control',{action:a})}catch(e){el('notice').textContent='暂时未能发送，请稍后重试';}}
el('mode').onchange=()=>post('/api/mode',{mode:el('mode').value}).catch(()=>{el('notice').textContent='模式未保存，请重试'});
document.querySelectorAll('[data-action]').forEach(b=>b.onclick=()=>action(b.dataset.action));
el('search').onsubmit=async e=>{e.preventDefault();try{await post('/api/search',{q:el('query').value});el('notice').textContent='正在搜索，请稍候…'}catch(e){el('notice').textContent='搜索未发送，请稍后重试'}};
async function refresh(){if(locked)return;locked=true;try{const r=await fetch('/api/state',{cache:'no-store'});if(!r.ok)throw Error();const s=await r.json();revision=s.revision;el('mode').value=s.mode;if(!el('query').value&&s.query)el('query').value=s.query;el('status').textContent=s.searching?'正在搜索…':(s.playing?(s.paused?'已暂停':'正在播放'):(s.loggedIn?'已登录 · 就绪':'请先扫码登录'));const messages={'Song unavailable - K1 next':'这个版本暂时不能播放，请换一首','Unable to play this version':'音频播放失败，请尝试其他版本','Wi-Fi offline - reconnecting':'Wi-Fi 已断开，正在重连','Finished':'播放结束','Stopped':'已停止','Getting playback URL...':'正在获取播放地址…','Searching...':'正在搜索…'};if(messages[s.message])el('notice').textContent=messages[s.message];el('vol').textContent=s.volume+'%';el('toggle').textContent=s.playing&&!s.paused?'暂停':(s.paused?'继续':'播放');const key=JSON.stringify([s.songs,s.selected,s.playing]);if(key!==lastSongs){lastSongs=key;el('songs').replaceChildren();s.songs.forEach((name,i)=>{const row=document.createElement('div');row.className='song'+(s.selected===i?' active':'');const t=document.createElement('span');t.textContent=(i+1)+'. '+name.replace(/<\/?em>/g,'');const b=document.createElement('button');b.textContent='播放';b.onclick=()=>post('/api/play',{index:i,revision}).catch(()=>{el('notice').textContent='播放未发送，请重试'});row.append(t,b);el('songs').append(row)});if(!s.songs.length){const p=document.createElement('p');p.textContent='暂时没有歌曲，请搜索。';el('songs').append(p)}}}catch(e){el('status').textContent='播放器正在处理请求或连接暂时中断';}finally{locked=false;setTimeout(refresh,2000)}}refresh();
</script></html>)PAGE";
static bool acceptPhoneRequest() {
 String origin=phoneServer.header("Origin");
 if(origin.length() && origin!="http://"+phoneServer.hostHeader()) {
  phoneServer.send(403,"text/plain; charset=utf-8","Origin rejected");return false;
 }
 return true;
}
static void servicePhoneUi() {if(phoneStarted)phoneServer.handleClient();}
static void beginPhoneUi() {
 const char* headers[]={"Origin"};phoneServer.collectHeaders(headers,1);
 phoneServer.on("/",HTTP_GET,[](){phoneServer.send_P(200,"text/html; charset=utf-8",PHONE_HTML);});
 phoneServer.on("/api/state",HTTP_GET,[](){
  cJSON* root=cJSON_CreateObject();
  if(!root){phoneServer.send(503,"text/plain","Low memory");return;}
  cJSON_AddBoolToObject(root,"playing",playbackActive);cJSON_AddBoolToObject(root,"paused",playbackPaused);
  cJSON_AddBoolToObject(root,"loggedIn",sessionToken.length()>0);
  cJSON_AddBoolToObject(root,"searching",searchQueued);
  cJSON_AddNumberToObject(root,"mode",playMode);cJSON_AddNumberToObject(root,"revision",listRevision);
  cJSON_AddNumberToObject(root,"queued",commandCount);
  cJSON_AddStringToObject(root,"message",statusMessage.c_str());
  cJSON_AddStringToObject(root,"query",rememberedQuery.c_str());
  cJSON_AddBoolToObject(root,"online",WiFi.status()==WL_CONNECTED);
  cJSON_AddNumberToObject(root,"volume",volumePercent);cJSON_AddNumberToObject(root,"selected",selectedSong);
  cJSON* songs=cJSON_AddArrayToObject(root,"songs");
  for(int i=0;i<musicCount;i++)cJSON_AddItemToArray(songs,cJSON_CreateString(musicResults[i].title.c_str()));
  char* json=cJSON_PrintUnformatted(root);
  phoneServer.sendHeader("Cache-Control","no-store");
  if(json)phoneServer.send(200,"application/json; charset=utf-8",json);else phoneServer.send(503,"text/plain","Low memory");
  if(json)cJSON_free(json);cJSON_Delete(root);
 });
 phoneServer.on("/api/search",HTTP_POST,[](){
  if(!acceptPhoneRequest())return;
  String q=phoneServer.arg("q");q.trim();
  if(!q.length()||q.length()>180){phoneServer.send(400,"text/plain","Invalid keyword");return;}
  clearCommands();pendingSearch=q;searchQueued=true;
  if(playbackActive){stopPlayback=true;afterPlayback='W';}
  phoneServer.send(202,"text/plain","Queued");
 });
 phoneServer.on("/api/play",HTTP_POST,[](){
  if(!acceptPhoneRequest())return;
  String value=phoneServer.arg("index");
  if(value.length()!=1||value[0]<'0'||value[0]>='0'+musicCount){phoneServer.send(400,"text/plain","Invalid song");return;}
  if(phoneServer.arg("revision")!=String(listRevision)){phoneServer.send(409,"text/plain","List changed; refresh");return;}
  if(!enqueueCommand('1'+value[0]-'0')){phoneServer.send(429,"text/plain","Command queue full");return;}
  phoneServer.send(202,"text/plain","Queued");
 });
 phoneServer.on("/api/control",HTTP_POST,[](){
  if(!acceptPhoneRequest())return;
  String a=phoneServer.arg("action");char c=0;
  if(a=="toggle")c='P';else if(a=="next")c=playbackActive?'N':'A';
  else if(a=="up")c='+';else if(a=="down")c='-';else if(a=="login")c='L';
  else if(a=="stop") {clearCommands();searchQueued=false;pendingSearch="";if(playbackActive){stopPlayback=true;afterPlayback=0;}}
  else {phoneServer.send(400,"text/plain","Invalid command");return;}
  if(c&&!enqueueCommand(c)){phoneServer.send(429,"text/plain","Command queue full");return;}
  phoneServer.send(202,"text/plain","Queued");
 });
 phoneServer.on("/api/mode",HTTP_POST,[](){
  if(!acceptPhoneRequest())return;
  String value=phoneServer.arg("mode");
  if(value.length()!=1||value[0]<'0'||value[0]>'3'){phoneServer.send(400,"text/plain","Invalid mode");return;}
  playMode=value.toInt();markSettings();phoneServer.send(200,"text/plain","OK");
 });
 phoneServer.onNotFound([](){phoneServer.send(404,"text/plain","Not found");});
 phoneServer.begin();phoneStarted=true;
 if(MDNS.begin("stm32music"))MDNS.addService("http","tcp",80);
 Serial.print("Phone control: http://");Serial.println(WiFi.localIP());
}
static void serviceNetwork() {
 static bool wasOnline=false;
 static uint32_t lastRetry=0;
 bool online=WiFi.status()==WL_CONNECTED;
 if(!online) {
  if(wasOnline) {Serial.println("Wi-Fi disconnected");screenStatus("Wi-Fi offline - reconnecting");qrKey="";}
  if(millis()-lastRetry>=10000 && strlen(WIFI_SSID)){lastRetry=millis();WiFi.reconnect();}
 } else if(!wasOnline) {
  if(!phoneStarted)beginPhoneUi();
  configTime(0,0,"pool.ntp.org","time.cloudflare.com","ntp.aliyun.com");
  Serial.print("Wi-Fi connected, phone: http://");Serial.println(WiFi.localIP());
  if(!playbackActive)screenStatus("Wi-Fi connected");
 }
 wasOnline=online;
}
static void processPhoneSearch() {
 if(!searchQueued||playbackActive)return;
 String keyword=pendingSearch;searchQueued=false;pendingSearch="";
 rememberedQuery=keyword;markSettings();
 screenStatus("Searching...");searchMusic(keyword);syncSongList();
}
