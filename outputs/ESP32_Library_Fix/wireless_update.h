#pragma once
#include <Update.h>
#include "esp_ota_ops.h"
#include "fw_package.h"
static bool otaArmRequested=false,otaUploading=false,otaEspSuccess=false,otaStmSuccess=false,otaWaitingStm=false;
static uint32_t otaExpires=0,otaRestartAt=0,otaStmStarted=0;
static unsigned otaAttempts=0,otaTarget=0,otaBuffered=0;
static uint32_t otaExpected=0,otaReceived=0,otaWholeCrc=0xffffffffUL,otaPayloadCrc=0xffffffffUL;
static uint8_t otaBlock[512],otaHeader[72];
static String otaPin,otaError,otaPhase="未进入升级模式";
static const char OTA_HTML[] PROGMEM=R"OTA(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>播放器无线升级</title><style>body{font:16px system-ui;background:#f2f5f7;color:#183c43;max-width:620px;margin:24px auto;padding:16px}section{background:white;padding:20px;border-radius:16px;margin:16px 0}button{background:#176d5d;color:white;border:0;border-radius:10px;padding:12px;margin:8px 0}input,select{display:block;max-width:100%;margin:12px 0;padding:10px}progress{width:100%;height:24px}.hint{color:#526c74}#status{white-space:pre-wrap}</style><h1>播放器无线升级</h1><a href="/">返回播放器</a><section><p>首次必须已安装 v26 引导程序和双程序分区。</p><button id="arm">进入升级模式</button><p class="hint">音乐会停止。请查看开发板屏幕显示的8位升级码，有效期3分钟。</p></section><section><label>升级码<input id="pin" inputmode="numeric" maxlength="8" autocomplete="off"></label><label>升级设备<select id="target"><option value="esp">ESP32（应用 .bin）</option><option value="stm">STM32（专用 .stmfw 包）</option></select></label><input id="file" type="file" accept=".bin,.stmfw"><button id="send">上传并升级</button><button id="cancel">退出升级模式</button><progress id="progress" max="100" value="0"></progress><p id="status">等待操作</p><p class="hint">上传时保持供电。ESP32请选择应用bin，不选merged/bootloader/partitions文件；STM32只选专用stmfw包。STM32写入完成前保持TF卡插入。</p></section><script>
const $=id=>document.getElementById(id);let busy=false;
$('arm').onclick=async()=>{try{const r=await fetch('/upgrade/arm',{method:'POST'});$('status').textContent=await r.text()}catch(e){$('status').textContent='无法连接设备'}};
$('cancel').onclick=async()=>{try{const r=await fetch('/upgrade/cancel',{method:'POST',headers:{Authorization:'Basic '+btoa('admin:'+$('pin').value)}});$('status').textContent=await r.text()}catch(e){$('status').textContent='无法连接设备'}};
$('send').onclick=()=>{const f=$('file').files[0],t=$('target').value;if(!f||!/^\d{8}$/.test($('pin').value)){alert('请选择文件并输入8位升级码');return}if(t==='stm'&&!f.name.endsWith('.stmfw')){alert('STM32请选择stmfw包');return}if(t==='esp'&&(!f.name.endsWith('.bin')||/merged|bootloader|partitions/.test(f.name))){alert('请选择ESP32应用bin');return}busy=true;$('send').disabled=true;const x=new XMLHttpRequest();x.open('POST','/upgrade/'+t+'?size='+f.size);x.setRequestHeader('Authorization','Basic '+btoa('admin:'+$('pin').value));x.upload.onprogress=e=>{if(e.lengthComputable)$('progress').value=e.loaded/e.total*100};x.onload=()=>{busy=false;$('send').disabled=false;$('status').textContent=x.responseText};x.onerror=()=>{busy=false;$('send').disabled=false;$('status').textContent='连接中断，请等设备恢复后检查状态；不要立即断电'};const data=new FormData();data.append('firmware',f);x.send(data)};
setInterval(async()=>{if(busy)return;try{const r=await fetch('/upgrade/status'),s=await r.json();$('status').textContent=s.phase+(s.error?'\n'+s.error:'');if(s.total)$('progress').value=s.bytes/s.total*100}catch(e){}},2000);
</script></html>)OTA";
static bool otaOrigin(){String origin=phoneServer.header("Origin");if(origin.length()&&origin!="http://"+phoneServer.hostHeader()){phoneServer.send(403,"text/plain","Origin rejected");return false;}return true;}
static bool otaAuthenticated(){
 if(!otaMaintenance||!otaPin.length()||(int32_t)(millis()-otaExpires)>=0||otaAttempts>=5)return false;
 if(!phoneServer.authenticate("admin",otaPin.c_str())){otaAttempts++;return false;}return true;
}
static void otaFail(const String& error){otaError=error;otaPhase="升级失败，原程序未确认替换";if(otaTarget==1)Update.abort();if(otaTarget==2)cardCommand("X","SD_STOPPED",3000);otaUploading=false;}
static bool otaStoreBlock(){
 if(!otaBuffered)return true;
 unsigned valid=otaBuffered;if(valid<512)memset(otaBlock+valid,0,512-valid);
 if(!waitReady(HIGH))return false;
 SPI.beginTransaction(SPISettings(2000000,MSBFIRST,SPI_MODE0));digitalWrite(AUDIO_CS_PIN,LOW);SPI.transferBytes(otaBlock,nullptr,512);bool ok=waitReady(LOW);digitalWrite(AUDIO_CS_PIN,HIGH);SPI.endTransaction();
 if(ok)otaWholeCrc=fwCrc(otaWholeCrc,otaBlock,valid);otaBuffered=0;return ok;
}
static void otaUpload(unsigned target){
 HTTPUpload& up=phoneServer.upload();
 if(up.status==UPLOAD_FILE_START){
  if(otaEspSuccess||otaStmSuccess||otaWaitingStm){otaError="一份请求仅允许一个升级文件，请等待重启完成";return;}
  otaTarget=target;otaError="";otaEspSuccess=otaStmSuccess=false;otaReceived=otaExpected=0;otaBuffered=0;otaWholeCrc=otaPayloadCrc=0xffffffffUL;memset(otaHeader,0,sizeof(otaHeader));
  if(!otaOrigin()){otaError="请求来源不匹配";return;}
  if(!otaAuthenticated()){otaError="升级码错误、失效或尝试过多，请重新进入升级模式";return;}
  if(playbackActive||saveMode){otaError="请先停止播放或下载";return;}
  String size=phoneServer.arg("size");for(size_t i=0;i<size.length();i++)if(size[i]<'0'||size[i]>'9'){otaError="文件大小无效";return;}
  otaExpected=(uint32_t)strtoul(size.c_str(),nullptr,10);
  const esp_partition_t* slot=esp_ota_get_next_update_partition(nullptr);
  if(!otaExpected||(target==1&&(!slot||otaExpected>slot->size))||(target==2&&(otaExpected<320||otaExpected>FW_APP_END-FW_APP_BASE+64))){otaError="文件大小超限或缺少OTA备用分区，请先有线安装v26";return;}
  if(target==1&&!Update.begin(otaExpected,U_FLASH)){otaError=String("ESP32准备失败：")+Update.errorString();return;}
  if(target==2&&(!cardCommand("BL","SD_BOOT:",3000)||cardReply!="SD_BOOT:26")){otaError="STM32专用引导程序未安装，请先完成首次有线安装";return;}
  if(target==2&&!cardCommand("FU:"+String(otaExpected),"SD_READY",5000)){otaError="STM32未接受升级文件，请检查v26程序和TF卡";return;}
  otaUploading=true;otaPhase=target==1?"正在接收ESP32固件":"正在将STM32升级包写入TF卡";
 }else if(up.status==UPLOAD_FILE_WRITE){
  if(!otaUploading)return;
  if(up.currentSize>otaExpected-otaReceived){otaFail("实际文件超出声明长度");return;}
  if(target==1){if(Update.write(up.buf,up.currentSize)!=up.currentSize){otaFail(String("ESP32写入失败：")+Update.errorString());return;}}
  else {
   unsigned n;uint32_t before=otaReceived;
   if(before<72){n=min((unsigned)up.currentSize,(unsigned)(72-before));memcpy(otaHeader+before,up.buf,n);}
   if(before<72&&before+up.currentSize>=72&&(!fwHeaderValid(otaHeader,otaExpected)||!fwVectorsValid(fwRead32(otaHeader+64),fwRead32(otaHeader+68),fwRead32(otaHeader+8)))){otaFail("STM32升级包机型、地址或向量无效");return;}
   unsigned skip=before<64?min((unsigned)(64-before),(unsigned)up.currentSize):0;
   otaPayloadCrc=fwCrc(otaPayloadCrc,up.buf+skip,up.currentSize-skip);
   unsigned at=0;while(at<up.currentSize){n=min((unsigned)up.currentSize-at,512-otaBuffered);memcpy(otaBlock+otaBuffered,up.buf+at,n);otaBuffered+=n;at+=n;if(otaBuffered==512&&!otaStoreBlock()){otaFail("STM32传输未确认，临时升级包未启用");return;}}
  }
  otaReceived+=up.currentSize;
 }else if(up.status==UPLOAD_FILE_END){
  if(!otaUploading)return;
  if(otaReceived!=otaExpected){otaFail("固件接收不完整");return;}
  if(target==1){if(!Update.end()){otaFail(String("ESP32校验失败：")+Update.errorString());return;}otaEspSuccess=true;otaPhase="ESP32固件已校验，即将重启";}
  else {
   if((otaPayloadCrc^0xffffffffUL)!=fwRead32(otaHeader+12)){otaFail("STM32固件CRC不匹配，未启用");return;}
   if(!otaStoreBlock()){otaFail("STM32尾包传输失败");return;}
   char command[64];snprintf(command,sizeof(command),"E:%lu:%08lX",(unsigned long)otaExpected,(unsigned long)(otaWholeCrc^0xffffffffUL));
   if(!cardCommand(command,"SD_DONE:",15000)){otaFail("STM32升级包保存失败："+cardStatus);return;}otaStmSuccess=true;otaPhase="STM32升级包已保存，准备重启写入";
  }
  otaUploading=false;
 }else if(up.status==UPLOAD_FILE_ABORTED){otaFail("上传中断，未完整接收的固件不启用");}
}
static void otaEndUpload(){
 if(otaError.length()){phoneServer.send(400,"text/plain; charset=utf-8",otaError);return;}
 if(!otaEspSuccess&&!otaStmSuccess){phoneServer.send(400,"text/plain; charset=utf-8","未收到有效升级文件");return;}
 phoneServer.send(200,"text/plain; charset=utf-8",otaPhase);
 if(otaEspSuccess)otaRestartAt=millis()+1200;
}
static void registerOtaRoutes(){
 phoneServer.on("/upgrade",HTTP_GET,[](){phoneServer.send_P(200,"text/html; charset=utf-8",OTA_HTML);});
 phoneServer.on("/upgrade/arm",HTTP_POST,[](){if(!otaOrigin())return;if(otaUploading||otaWaitingStm||saveMode){phoneServer.send(409,"text/plain; charset=utf-8","设备正在写入或下载，请稍后");return;}otaArmRequested=true;stopPlayback=true;afterPlayback=0;queuedOnlinePlay=-1;favoritePlay=-1;favoriteRequestPage=0;consoleJob="";cardRequest="";searchQueued=false;qrKey="";phoneServer.send(202,"text/plain; charset=utf-8","请查看开发板屏幕的8位升级码");});
 phoneServer.on("/upgrade/cancel",HTTP_POST,[](){if(!otaOrigin())return;if(!otaAuthenticated()||otaUploading||otaWaitingStm){phoneServer.send(403,"text/plain; charset=utf-8","升级码无效或正在写入");return;}otaPin="";otaMaintenance=false;otaPhase="已退出升级模式";syncConsoleList();phoneServer.send(200,"text/plain; charset=utf-8",otaPhase);});
 phoneServer.on("/upgrade/status",HTTP_GET,[](){cJSON* j=cJSON_CreateObject();cJSON_AddStringToObject(j,"phase",otaPhase.c_str());cJSON_AddStringToObject(j,"error",otaError.c_str());cJSON_AddNumberToObject(j,"bytes",otaReceived);cJSON_AddNumberToObject(j,"total",otaExpected);char* json=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(json){phoneServer.send(200,"application/json; charset=utf-8",json);free(json);}else phoneServer.send(503,"text/plain","Low memory");});
 phoneServer.on("/upgrade/esp",HTTP_POST,otaEndUpload,[](){otaUpload(1);});
 phoneServer.on("/upgrade/stm",HTTP_POST,otaEndUpload,[](){otaUpload(2);});
}
static void serviceOta(){
 if(otaRestartAt&&(int32_t)(millis()-otaRestartAt)>=0){ESP.restart();return;}
 if(otaArmRequested&&!playbackActive&&!saveMode){otaArmRequested=false;stopCardLocal();cardCommand("X","SD_STOPPED",3000);otaMaintenance=true;otaError="";otaAttempts=0;char pin[12];snprintf(pin,sizeof(pin),"%08lu",(unsigned long)(esp_random()%100000000UL));otaPin=pin;otaExpires=millis()+180000;otaPhase="升级模式已就绪，请输入屏幕上的升级码";consoleMessage("Wi-Fi 升级码：\n"+otaPin+"\n3分钟内有效\n打开 /upgrade 上传固件");}
 if(otaStmSuccess&&!otaWaitingStm){otaStmSuccess=false;if(!cardCommand("RB","SD_REBOOTING",3000)){otaError="STM32未确认重启，升级包仍保留在TF卡";return;}otaWaitingStm=true;otaStmStarted=millis();screenReady=false;otaPhase="STM32正在校验和写入，请保持TF卡及供电";}
 if(otaWaitingStm&&screenReady){otaWaitingStm=false;otaPhase="STM32已重新启动，升级完成";otaMaintenance=false;otaPin="";cardCommand("V","SD_LIST_END",20000);syncConsoleList();consoleMessage(otaPhase);}
 if(otaWaitingStm&&millis()-otaStmStarted>120000){otaError="STM32尚未恢复，请保持TF卡并查看引导程序日志";}
 if(otaMaintenance&&!otaUploading&&!otaWaitingStm&&!otaEspSuccess&&(int32_t)(millis()-otaExpires)>=0){otaMaintenance=false;otaPin="";otaPhase="升级码已过期，请重新进入升级模式";syncConsoleList();}
}
