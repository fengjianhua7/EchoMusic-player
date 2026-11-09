#pragma once
static bool playerInitialized=false;
static bool awaitScreenReady(uint32_t timeout){
 uint32_t start=millis(),lastProbe=millis()-1000,lastLog=millis()-5000;String reply;
 while(millis()-start<timeout){
  if(millis()-lastProbe>=1000){lastProbe=millis();screenLink.println("HI");screenLink.flush();}
  while(readLinkLine(reply)){if(reply.startsWith("CAL_"))Serial.println(reply);if(reply=="UI_READY:25"){screenReady=true;delay(30);clearScreenRx();Serial.println("STM32 v25 ready; calibration complete");return true;}}
  if(millis()-lastLog>=5000){lastLog=millis();Serial.println("Waiting for STM32 v25 / touch calibration...");}
  delay(1);
 }
 return false;
}
static void serviceNetworkStartup(){
 if(!playerInitialized)return;
 if(WiFi.status()!=WL_CONNECTED){networkStarted=false;return;}
 if(networkStarted)return;
 networkStarted=true;if(!phoneStarted)beginPhoneUi();
 configTime(0,0,"pool.ntp.org","time.cloudflare.com","ntp.aliyun.com");
 Serial.print("Wi-Fi OK, IP: ");Serial.println(WiFi.localIP());
}
static void startPlayerAfterScreen(){
 if(playerInitialized)return;playerInitialized=true;
 WiFi.mode(WIFI_STA);initMusicIdentity();
 browseSource=SOURCE_CARD;browsePage=0;localFilter="";consoleJob="CARD";
 processConsoleActions(); // Load local files before any online search or login request.
 if(strlen(WIFI_SSID)){WiFi.setAutoReconnect(true);WiFi.begin(WIFI_SSID,WIFI_PASSWORD);Serial.println("Wi-Fi connecting in background; local player ready");}
 else Serial.println("No Wi-Fi configured; local player ready");
 Serial.println("Commands: login | s keyword | 1..5 play | p test | i status");
}
