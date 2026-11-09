#pragma once
#include "qrcodegen.h"
static HardwareSerial screenLink(1);
static String qrKey;
static uint32_t qrStarted=0,qrLastPoll=0;
static void screenStatus(const char* text) {
 screenLink.print("S:");screenLink.println(text);screenLink.flush();
}
static bool showQr(const String& text) {
 static uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
 static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
 if(!qrcodegen_encodeText(text.c_str(),temp,qr,qrcodegen_Ecc_LOW,1,10,qrcodegen_Mask_AUTO,true))return false;
 int n=qrcodegen_getSize(qr);uint8_t packed[407]={0};
 for(int y=0;y<n;y++)for(int x=0;x<n;x++) {
  int k=y*n+x;if(qrcodegen_getModule(qr,x,y))packed[k/8]|=1<<(k%8);
 }
 Serial.printf("Sending screen QR: size=%d, bytes=%d\n",n,(n*n+7)/8);
 while(screenLink.available())screenLink.read();
 screenLink.printf("Q:%d:",n);
 for(int i=0;i<(n*n+7)/8;i++)screenLink.printf("%02X",packed[i]);
 screenLink.println();screenLink.flush();
 uint32_t start=millis();String ack;
 while(millis()-start<5000) {
  while(screenLink.available()) {
   char c=(char)screenLink.read();
   if(c=='\n') {ack.trim();if(ack=="QR_OK") {Serial.printf("Screen QR confirmed, size=%d\n",n);return true;}
     if(ack.startsWith("QR_ERR:")){Serial.println(ack);return false;}ack="";}
   else if(ack.length()<40)ack+=c;
  }
  delay(1);
 }
 Serial.println("Screen QR acknowledgement missing. Check GPIO17->B11 and B10->GPIO18.");
 return false;
}
static void beginQrLogin() {
 qrKey="";
 cJSON* doc=apiGet("/v2/qrcode","",{{"appid","1001"},{"type","1"},{"plat","4"},
  {"qrcode_txt","https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=3116&"},{"srcappid","2919"}},true);
 if(!doc){screenStatus("Login request failed");return;}
 qrKey=jsonText(cJSON_GetObjectItemCaseSensitive(doc,"data"),"qrcode");
 cJSON_Delete(doc);
 if(!qrKey.length()){Serial.println("QR key missing in response");screenStatus("QR key unavailable");return;}
 String link="https://h5.kugou.com/apps/loginQRCode/html/index.html?qrcode="+qrKey;
 if(!showQr(link)){qrKey="";Serial.println("Login cancelled: screen did not confirm new QR");return;}
 qrStarted=millis();qrLastPoll=millis();
 Serial.println("Scan QR on STM32 using Kugou app, then confirm. Expires after 2 minutes.");
}
static void pollQrLogin() {
 if(!qrKey.length())return;
 if(millis()-qrStarted>120000){qrKey="";screenStatus("QR expired. Send login");Serial.println("QR expired; send login again");return;}
 if(millis()-qrLastPoll<2500)return;
 qrLastPoll=millis();
 cJSON* doc=apiGet("/v2/get_userinfo_qrcode","",{{"plat","4"},{"appid","3116"},
  {"srcappid","2919"},{"qrcode",qrKey},{"dev",musicMid}},true);
 if(!doc){qrKey="";screenStatus("Login check failed");return;}
 cJSON* data=cJSON_GetObjectItemCaseSensitive(doc,"data");
 String state=jsonText(data,"status");
 Serial.print("QR state: ");Serial.println(state);
 if(state=="4") {
  String token=jsonText(data,"token"),uid=jsonText(data,"userid");
  if(token.length()&&uid.length()&&uid!="0") {
   Preferences prefs;prefs.begin("directmusic",false);
   bool saved=prefs.putString("userid",uid)>0 && prefs.putString("token",token)>0;prefs.end();
   sessionUser=uid;sessionToken=token;qrKey="";
   screenStatus(saved?"Login saved. Search a song":"Logged in; save failed");
   Serial.println(saved?"Login successful; session saved locally":"Login successful; persistent save failed");
  } else {qrKey="";screenStatus("Login session incomplete");}
 } else if(state=="2")screenStatus("Confirm on phone");
 else if(state=="0"){qrKey="";screenStatus("QR expired. Send login");}
 cJSON_Delete(doc);
}
static void logoutMusic() {
 qrKey="";sessionUser="0";sessionToken="";
 Preferences p;p.begin("directmusic",false);p.remove("userid");p.remove("token");p.end();
 screenStatus("Logged out");Serial.println("Stored session removed");
}
