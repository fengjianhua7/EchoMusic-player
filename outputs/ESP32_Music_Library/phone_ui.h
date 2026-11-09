#pragma once
#include <WebServer.h>
#include <ESPmDNS.h>
static WebServer phoneServer(80);
static bool phoneStarted=false;
static String pendingSearch;
static bool searchQueued=false;
static const char PHONE_HTML[] PROGMEM=R"PAGE(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="theme-color" content="#173b36">
<title>随声 · 音乐播放器</title><style>
:root{--ink:#18342f;--muted:#64776f;--green:#1c6b55;--line:#dce6dc;--paper:#f4f6ef}*{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font:15px system-ui,"Microsoft YaHei",sans-serif}button,input{font:inherit}button{white-space:nowrap;flex-shrink:0;cursor:pointer;border:0;border-radius:12px;background:var(--green);color:white;padding:11px 17px;font-weight:600}button:disabled{opacity:.4;cursor:default}input{border:1px solid var(--line);border-radius:12px;background:white;padding:13px;min-width:0;width:100%;outline-color:var(--green)}main{max-width:900px;margin:auto;padding:28px 24px 170px}header{display:flex;justify-content:space-between;align-items:center;margin-bottom:26px}.brand{font-size:28px;letter-spacing:.1em;font-weight:750}.eyebrow{font-size:12px;letter-spacing:.12em;color:var(--muted);margin-top:5px}.account{background:#e4ebdf;color:var(--green)}.hero{background:#173b36;color:#f8fff5;border-radius:24px;padding:28px;position:relative;overflow:hidden}.hero:after{content:'♪';font-size:160px;position:absolute;right:24px;top:-30px;opacity:.08;pointer-events:none}.hero small{color:#b9d7c7;letter-spacing:.1em}.hero h1{font-size:27px;margin:12px 0;max-width:85%;line-height:1.5;overflow-wrap:anywhere}.hero p{margin:0;color:#c8dbce}.tabs{display:flex;gap:8px;margin:24px 0 20px;padding:5px;background:#e7ecdf;border-radius:15px}.tabs button{flex:1;background:transparent;color:var(--muted);white-space:nowrap}.tabs button[aria-selected=true]{background:white;color:var(--green);box-shadow:0 2px 6px #19352f09}.panel[hidden]{display:none}h2{font-size:21px;margin:0}.heading{display:flex;justify-content:space-between;align-items:center;gap:12px;margin:22px 0 14px}.heading p{font-size:13px;color:var(--muted);margin:5px 0}form{display:flex;gap:9px;margin-bottom:10px}.song{display:flex;gap:12px;align-items:center;padding:17px 0;border-bottom:1px solid var(--line)}.song.active .name{color:var(--green);font-weight:700}.number{width:26px;color:#8a9c91;font-size:13px;flex-shrink:0}.details{flex:1;min-width:0}.name{line-height:1.6;overflow-wrap:anywhere}.meta{font-size:12px;color:var(--muted);margin-top:3px}.song button{padding:9px 12px;font-size:13px;flex-shrink:0}.secondary{background:#e6eddf;color:var(--green)}.empty{padding:35px 18px;text-align:center;color:var(--muted);border:1px dashed #cbd7c8;border-radius:16px;margin-top:15px;line-height:1.8}.hint{font-size:13px;color:var(--muted);line-height:1.7}#notice{min-height:24px;margin:12px 0;color:var(--green);font-size:13px}.pagination{display:flex;justify-content:center;gap:18px;align-items:center;margin:23px 0}.dock{position:fixed;bottom:0;left:0;right:0;background:#ffffffed;border-top:1px solid var(--line);backdrop-filter:blur(12px);z-index:2}.dock-inner{max-width:900px;margin:auto;padding:17px 24px;display:flex;align-items:center;gap:10px}.transport{display:flex;gap:8px;flex:1}.transport button{min-width:70px}.volume{display:flex;align-items:center;gap:10px}.volume button{padding:9px 13px;background:#e6eddf;color:var(--green)}#vol{min-width:40px;text-align:center;font-size:13px}.connection{font-size:12px;color:var(--muted);padding:0 24px 12px;max-width:900px;margin:auto}#status{font-weight:600}small{line-height:1.7}@media(max-width:560px){main{padding:20px 16px 195px}.hero{padding:22px}.hero h1{font-size:22px}.dock-inner{flex-wrap:wrap;padding:14px 16px}.transport{width:100%}.transport button{flex:1}.volume{width:100%;justify-content:center}.song{gap:8px}.song button{padding:9px}.heading{align-items:flex-start}.brand{font-size:25px}.account{font-size:13px;padding:10px}.tabs button{padding:11px 5px}.connection{padding-left:16px}}
</style></head><body><main>
<header><div><div class="brand">随声</div><div class="eyebrow">让喜欢的音乐，一直在身边</div></div><button class="account" data-action="login" id="loginButton">扫码登录</button></header>
<section class="hero"><small>NOW PLAYING · 此刻播放</small><h1 id="nowTitle">选一首喜欢的歌</h1><p id="status" role="status" aria-live="polite">正在连接播放器…</p></section>
<nav class="tabs" aria-label="音乐来源"><button data-tab="online" aria-selected="true">在线搜索</button><button data-tab="favorite" aria-selected="false">♡ 我喜欢</button><button data-tab="local" aria-selected="false">内存卡</button></nav>
<div id="notice" role="status" aria-live="polite"></div>
<section class="panel" id="online"><form id="search"><input id="query" aria-label="搜索在线歌曲" placeholder="歌曲名、歌手，或今天的心情" maxlength="60" required><button>搜索</button></form><div class="heading"><div><h2>发现音乐</h2><p>保留 VIP，排除需要单独付费的版本</p></div></div><div id="songs"></div></section>
<section class="panel" id="favorite" hidden><div class="heading"><div><h2>我的喜欢</h2><p id="favoriteStatus">登录后读取酷狗「我喜欢」</p></div><button id="loadFavorites">同步列表</button></div><p class="hint">读取已登录酷狗账号的云端歌单，不会修改你的收藏。每页 5 首。</p><div id="favoriteSongs"></div><div class="pagination"><button class="secondary" id="favoritePrev" disabled>上一页</button><span id="favoritePage">—</span><button class="secondary" id="favoriteNext" disabled>下一页</button></div></section>
<section class="panel" id="local" hidden><div class="heading"><div><h2>随身曲库</h2><p id="cardStatus">尚未读取内存卡</p></div><button id="scanCard">读取列表</button></div><input type="search" id="localQuery" placeholder="搜索内存卡歌曲名或歌手" aria-label="搜索内存卡歌曲"><p class="hint" id="localCount">读取列表后即可搜索</p><div id="localFiles"></div><p class="hint">读取卡根目录前 128 首 MP3 / WAV；下载的音乐保存为 WAV。未完成的文件不会出现在列表。</p></section>
</main><footer class="dock"><div class="dock-inner"><div class="transport"><button id="toggle" data-action="toggle">播放</button><button class="secondary" data-action="next">下一首</button><button class="secondary" data-action="stop">停止</button></div><div class="volume"><span class="hint">音量</span><button data-action="down" aria-label="降低音量">−</button><span id="vol">25%</span><button data-action="up" aria-label="增加音量">＋</button></div></div><div class="connection">通过同一 Wi-Fi 控制 · 登录二维码显示在开发板屏幕</div></footer>
<script>
const el=id=>document.getElementById(id);let locked=false,lastSongs='',lastLocal='',lastFavorite='',state=null;
function notify(text){el('notice').textContent=text}
async function post(path,body){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(body)});if(!r.ok)throw Error(await r.text());notify('操作已发送');}
function run(path,body){post(path,body).catch(e=>notify('操作未完成：'+(e.message||'请稍后重试')))}
document.querySelectorAll('[data-action]').forEach(b=>b.onclick=()=>run('/api/control',{action:b.dataset.action}));
document.querySelectorAll('[data-tab]').forEach(b=>b.onclick=()=>{document.querySelectorAll('[data-tab]').forEach(t=>t.setAttribute('aria-selected',String(t===b)));document.querySelectorAll('.panel').forEach(p=>p.hidden=p.id!==b.dataset.tab)});
el('search').onsubmit=e=>{e.preventDefault();run('/api/search',{q:el('query').value});notify('正在搜索，请稍候…')};
el('scanCard').onclick=()=>run('/api/card',{action:'list'});
el('loadFavorites').onclick=()=>run('/api/favorites',{action:'load',page:1});
el('favoritePrev').onclick=()=>run('/api/favorites',{action:'load',page:Math.max(1,(state?.favoritePage||1)-1)});
el('favoriteNext').onclick=()=>run('/api/favorites',{action:'load',page:(state?.favoritePage||0)+1});
function clean(s){return String(s).replace(/&amp;/g,'&').replace(/<\/?em>/g,'')}
function empty(container,message){const p=document.createElement('p');p.className='empty';p.textContent=message;container.append(p)}
function row(container,index,title,meta,actions,active=false){const r=document.createElement('div');r.className='song'+(active?' active':'');const num=document.createElement('span');num.className='number';num.textContent=String(index+1).padStart(2,'0');const detail=document.createElement('div');detail.className='details';const name=document.createElement('div');name.className='name';name.textContent=clean(title);detail.append(name);if(meta){const m=document.createElement('div');m.className='meta';m.textContent=meta;detail.append(m)}r.append(num,detail);actions.forEach(([label,fn],i)=>{const b=document.createElement('button');b.textContent=label;b.onclick=fn;if(i)b.className='secondary';r.append(b)});container.append(r)}
function renderLocal(){const box=el('localFiles');box.replaceChildren();if(!state){empty(box,'读取内存卡，发现你的随身音乐');return}const q=el('localQuery').value.trim().toLocaleLowerCase();let count=0;state.localFiles.forEach((name,i)=>{if(!clean(name).toLocaleLowerCase().includes(q))return;count++;row(box,i,name,((state.localFileSizes[i]||0)/1048576).toFixed(2)+' MB',[['播放',()=>run('/api/card',{action:'play',index:i})]])});el('localCount').textContent=q?'找到 '+count+' 首 / 共 '+state.localFiles.length+' 首':'共 '+state.localFiles.length+' 首 · 支持按歌曲名或歌手搜索';if(!count)empty(box,q?'没有匹配的歌曲，试试其他关键词':'点击「读取列表」加载内存卡歌曲')}
el('localQuery').oninput=renderLocal;
function render(s){if(!s.localChanged&&state){s.localFiles=state.localFiles;s.localFileSizes=state.localFileSizes}s.localFiles=s.localFiles||[];s.localFileSizes=s.localFileSizes||[];state=s;el('status').textContent=s.downloading?'正在下载到内存卡…':s.localPlaying?(s.paused?'本地已暂停':s.cardStatus):s.searching?'正在获取歌曲…':s.playing?(s.paused?'已暂停':'正在播放'):s.loggedIn?'已登录 · 就绪':'未登录 · 可播放内存卡歌曲';el('nowTitle').textContent=s.nowTitle||'选一首喜欢的歌';el('vol').textContent=s.volume+'%';el('toggle').textContent=s.playing&&!s.paused?'暂停':s.paused?'继续':'播放';el('loginButton').textContent=s.loggedIn?'已登录 · 换账号':'扫码登录';el('cardStatus').textContent=s.cardStatus+(s.downloading?' · '+Math.round(s.downloadBytes/1024)+' KB':'');const lk=JSON.stringify([s.localFiles,s.localFileSizes]);if(lk!==lastLocal){lastLocal=lk;renderLocal()}const key=JSON.stringify([s.songs,s.selected,s.playing]);if(key!==lastSongs){lastSongs=key;const box=el('songs');box.replaceChildren();s.songs.forEach((name,i)=>row(box,i,name,'',[['播放',()=>run('/api/play',{index:i})],['下载',()=>run('/api/card',{action:'download',index:i})]],s.selected===i&&s.playing));if(!s.songs.length)empty(box,'搜索歌曲名或歌手，开始发现音乐')}el('favoriteStatus').textContent=s.favoriteStatus;el('loadFavorites').disabled=s.favoriteBusy;el('favoritePage').textContent=s.favoritePage?'第 '+s.favoritePage+' 页':'—';el('favoritePrev').disabled=s.favoriteBusy||s.favoritePage<=1;el('favoriteNext').disabled=s.favoriteBusy||!s.favoriteHasNext;const fk=JSON.stringify([s.favoriteRevision,s.favoriteSongs]);if(fk!==lastFavorite){lastFavorite=fk;const box=el('favoriteSongs');box.replaceChildren();s.favoriteSongs.forEach((name,i)=>row(box,i+(s.favoritePage-1)*5,name,'酷狗 · 我喜欢',[['播放',()=>run('/api/favorites',{action:'play',index:i,revision:s.favoriteRevision})]]));if(!s.favoriteSongs.length)empty(box,s.loggedIn?'点击「同步列表」读取你的喜欢':'扫码登录后，你喜欢的音乐会出现在这里')}}
async function refresh(){if(locked)return;locked=true;try{const r=await fetch('/api/state'+(state?'?localRevision='+state.localRevision:''),{cache:'no-store'});if(!r.ok)throw Error();render(await r.json())}catch(e){el('status').textContent='正在等待播放器响应…'}finally{locked=false;setTimeout(refresh,2000)}}refresh();
</script></body></html>
)PAGE";
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
  checkFavoriteAccount();
  cJSON_AddBoolToObject(root,"searching",searchQueued||favoriteRequestPage>0);
  cJSON_AddStringToObject(root,"favoriteStatus",favoriteStatus.c_str());
  cJSON_AddNumberToObject(root,"favoritePage",favoritePage);
  cJSON_AddNumberToObject(root,"favoriteRevision",favoriteRevision);
  cJSON_AddBoolToObject(root,"favoriteHasNext",favoriteHasNext);
  cJSON_AddBoolToObject(root,"favoriteBusy",favoriteRequestPage>0);
  cJSON* favorites=cJSON_AddArrayToObject(root,"favoriteSongs");
  for(int i=0;i<favoriteCount;i++)cJSON_AddItemToArray(favorites,cJSON_CreateString(favoriteSongs[i].title.c_str()));
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
 phoneServer.on("/api/favorites",HTTP_POST,[](){
  if(!acceptPhoneRequest())return;
  checkFavoriteAccount();
  if(!sessionToken.length()||sessionUser=="0"){phoneServer.send(401,"text/plain; charset=utf-8","请先扫码登录");return;}
  if(saveMode||favoriteRequestPage||favoritePlay>=0||searchQueued){phoneServer.send(409,"text/plain; charset=utf-8","设备正在处理请求，请稍后重试");return;}
  String action=phoneServer.arg("action");
  if(action=="load") {
   String value=phoneServer.arg("page");
   if(!value.length()||value.length()>5){phoneServer.send(400,"text/plain","Invalid page");return;}
   for(size_t i=0;i<value.length();i++)if(value[i]<'0'||value[i]>'9'){phoneServer.send(400,"text/plain","Invalid page");return;}
   int page=value.toInt();if(page<1||page>10000){phoneServer.send(400,"text/plain","Invalid page");return;}
   favoriteRequestPage=page;favoriteStatus="正在读取喜欢列表…";
  } else if(action=="play") {
   String value=phoneServer.arg("index");
   if(value.length()!=1||value[0]<'0'||value[0]>='0'+favoriteCount||phoneServer.arg("revision")!=String(favoriteRevision)){
    phoneServer.send(409,"text/plain; charset=utf-8","列表已变化，请刷新后重试");return;
   }
   favoritePlay=value[0]-'0';
  } else {phoneServer.send(400,"text/plain","Invalid action");return;}
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
