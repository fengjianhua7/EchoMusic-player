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
<div class="card"><label>音量</label><div class="volume"><button data-action="down" aria-label="降低音量">−</button><div id="vol">25%</div><button data-action="up" aria-label="增加音量">＋</button></div></div>
<div class="card"><h2>内存卡歌曲</h2><button id="scanCard">读取列表</button><p id="cardStatus">尚未读取内存卡</p><div id="localFiles"></div><small>下载保存为 WAV；未完成的文件不会出现在列表。</small></div><div class="card"><button data-action="login">扫码登录</button><p>登录二维码会显示在开发板屏幕，请用酷狗 APP 扫描。</p><small>列表仅排除付费购买版本，VIP 歌曲仍会保留。</small></div></main>
<script>
const el=id=>document.getElementById(id);let locked=false,lastSongs='',lastLocal='';
async function post(path,body){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(body)});if(!r.ok)throw Error(await r.text());el('notice').textContent='操作已发送';}
async function action(a){try{await post('/api/control',{action:a})}catch(e){el('notice').textContent='暂时未能发送，请稍后重试';}}
el('scanCard').onclick=()=>post('/api/card',{action:'list'}).catch(()=>{el('cardStatus').textContent='内存卡忙，请稍后重试'});
document.querySelectorAll('[data-action]').forEach(b=>b.onclick=()=>action(b.dataset.action));
el('search').onsubmit=async e=>{e.preventDefault();try{await post('/api/search',{q:el('query').value});el('notice').textContent='正在搜索，请稍候…'}catch(e){el('notice').textContent='搜索未发送，请稍后重试'}};
async function refresh(){if(locked)return;locked=true;try{const r=await fetch('/api/state',{cache:'no-store'});if(!r.ok)throw Error();const s=await r.json();el('cardStatus').textContent=s.cardStatus+(s.downloading?' '+Math.round(s.downloadBytes/1024)+' KB':'');const lk=JSON.stringify(s.localFiles);if(lk!==lastLocal){lastLocal=lk;el('localFiles').replaceChildren();s.localFiles.forEach((name,i)=>{const row=document.createElement('div');row.className='song';const t=document.createElement('span');t.textContent=name;const b=document.createElement('button');b.textContent='本地播放';b.onclick=()=>post('/api/card',{action:'play',index:i}).catch(()=>{el('cardStatus').textContent='本地播放请求失败'});row.append(t,b);el('localFiles').append(row)})}el('status').textContent=s.downloading?'正在下载到内存卡…':(s.localPlaying?s.cardStatus:(s.searching?'正在搜索…':(s.playing?(s.paused?'已暂停':'正在播放'):(s.loggedIn?'已登录 · 就绪':'请先扫码登录'))));el('vol').textContent=s.volume+'%';el('toggle').textContent=s.playing&&!s.paused?'暂停':(s.paused?'继续':'播放');const key=JSON.stringify([s.songs,s.selected,s.playing]);if(key!==lastSongs){lastSongs=key;el('songs').replaceChildren();s.songs.forEach((name,i)=>{const row=document.createElement('div');row.className='song'+(s.selected===i?' active':'');const t=document.createElement('span');t.textContent=(i+1)+'. '+name.replace(/<\/?em>/g,'');const b=document.createElement('button');b.textContent='播放';b.onclick=()=>post('/api/play',{index:i}).catch(()=>{el('notice').textContent='播放未发送，请重试'});const d=document.createElement('button');d.textContent='下载';d.onclick=()=>post('/api/card',{action:'download',index:i}).catch(()=>{el('notice').textContent='下载未开始，设备可能正在忙'});row.append(t,b,d);el('songs').append(row)});if(!s.songs.length){const p=document.createElement('p');p.textContent='暂时没有歌曲，请搜索。';el('songs').append(p)}}}catch(e){el('status').textContent='播放器正在处理请求或连接暂时中断';}finally{locked=false;setTimeout(refresh,2000)}}refresh();
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
  cJSON_AddNumberToObject(root,"volume",volumePercent);cJSON_AddNumberToObject(root,"selected",selectedSong);
  appendCardState(root);
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
  pendingSearch=q;searchQueued=true;
  if(playbackActive){stopPlayback=true;afterPlayback='W';}
  phoneServer.send(202,"text/plain","Queued");
 });
 phoneServer.on("/api/play",HTTP_POST,[](){
  if(!acceptPhoneRequest())return;
  String value=phoneServer.arg("index");
  if(value.length()!=1||value[0]<'0'||value[0]>='0'+musicCount){phoneServer.send(400,"text/plain","Invalid song");return;}
  boardCommand='1'+value[0]-'0';phoneServer.send(202,"text/plain","Queued");
 });
 phoneServer.on("/api/control",HTTP_POST,[](){
  if(!acceptPhoneRequest())return;
  String a=phoneServer.arg("action");char c=0;
  if(a=="toggle")c='P';else if(a=="next")c=playbackActive?'N':'A';
  else if(a=="up")c='+';else if(a=="down")c='-';else if(a=="login")c='L';
  else if(a=="stop") {requestCardStop();if(playbackActive){stopPlayback=true;afterPlayback=0;}}
  else {phoneServer.send(400,"text/plain","Invalid command");return;}
  if(c)boardCommand=c;phoneServer.send(202,"text/plain","Queued");
 });
 phoneServer.onNotFound([](){phoneServer.send(404,"text/plain","Not found");});
 registerCardRoutes();
 phoneServer.begin();phoneStarted=true;
 if(MDNS.begin("stm32music"))MDNS.addService("http","tcp",80);
 Serial.print("Phone control: http://");Serial.println(WiFi.localIP());
}
static void processPhoneSearch() {
 if(!searchQueued||playbackActive)return;
 String keyword=pendingSearch;searchQueued=false;pendingSearch="";
 screenStatus("Searching...");searchMusic(keyword);syncSongList();
}
