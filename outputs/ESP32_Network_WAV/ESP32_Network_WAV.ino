#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include "config.h"

static const int AUDIO_SCK_PIN=12, AUDIO_MOSI_PIN=11, AUDIO_CS_PIN=10, AUDIO_READY_PIN=9;
static uint8_t packet[512];
static bool waitReady(int level) {
  uint32_t start=millis();
  while(digitalRead(AUDIO_READY_PIN)!=level) {
    if(millis()-start>3000) return false;
    delay(1);
  }
  return true;
}
static bool sendBlock() {
  if(!waitReady(HIGH)) { Serial.println("STM32 AUDIO_READY_PIN timeout"); return false; }
  SPI.beginTransaction(SPISettings(1000000,MSBFIRST,SPI_MODE0));
  digitalWrite(AUDIO_CS_PIN,LOW);
  SPI.transferBytes(packet,nullptr,512);
  bool ok=waitReady(LOW);
  digitalWrite(AUDIO_CS_PIN,HIGH); SPI.endTransaction();
  if(!ok) {
    Serial.println("ACK timeout. Reset both boards.");
    while(true) delay(1000);
  }
  return true;
}
static bool exact(NetworkClient& in,uint8_t* dest,size_t count) {
  uint32_t last=millis();
  while(count) {
    int n=in.available();
    if(n>0) {
      size_t want=min(count,(size_t)n);
      int got=in.read(dest,want);
      if(got>0) {dest+=got; count-=got; last=millis();}
    } else {
      if(!in.connected() || millis()-last>10000) return false;
      delay(1);
    }
  }
  return true;
}
static uint16_t le16(const uint8_t* p) {return p[0]|((uint16_t)p[1]<<8);}
static uint32_t le32(const uint8_t* p) {return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static bool skip(NetworkClient& in,uint32_t n) {
  while(n) {size_t k=min(n,(uint32_t)sizeof(packet)); if(!exact(in,packet,k)) return false; n-=k;}
  return true;
}
static bool wav(NetworkClient& in) {
  uint8_t h[16];
  if(!exact(in,h,12)||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4)) {
    Serial.println("Not a RIFF WAV file"); return false;
  }
  uint32_t remaining=le32(h+4);
  if(remaining<4) return false;
  remaining-=4;
  bool format=false;
  while(remaining>=8) {
    if(!exact(in,h,8)) return false;
    remaining-=8;
    uint32_t n=le32(h+4);
    if(n>remaining) return false;
    if(!memcmp(h,"fmt ",4)) {
      if(n<16||!exact(in,h,16)) return false;
      format=le16(h)==1 && le16(h+2)==1 && le32(h+4)==32000 && le16(h+12)==2 && le16(h+14)==16;
      if(!format) {Serial.println("Required: PCM16 mono 32000Hz");return false;}
      if(!skip(in,n-16)) return false;
    } else if(!memcmp(h,"data",4)) {
      if(!format || (n&1)) return false;
      Serial.printf("Playing %lu bytes\n",(unsigned long)n);
      while(n) {
        size_t k=min(n,(uint32_t)512);
        memset(packet,0,512);
        if(!exact(in,packet,k)) {Serial.println("Audio download interrupted");return false;}
        // Low initial volume: quarter amplitude, signed PCM16.
        for(size_t i=0;i<k;i+=2) {
          int16_t v=(int16_t)le16(packet+i); v/=4;
          packet[i]=(uint8_t)v; packet[i+1]=(uint8_t)((uint16_t)v>>8);
        }
        if(!sendBlock()) return false;
        n-=k;
      }
      Serial.println("Playback complete"); return true;
    } else if(!skip(in,n)) return false;
    remaining-=n;
    if(n&1) {if(!remaining||!skip(in,1)) return false; remaining--;}
  }
  return false;
}
static void playUrl() {
  if(strncmp(WAV_URL,"http://",7)) {
    Serial.println("Set WAV_URL to an HTTP WAV link first"); return;
  }
  NetworkClient client;
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(10000);
  const char* keys[]={"Transfer-Encoding","Content-Encoding"};
  http.collectHeaders(keys,2);
  if(!http.begin(client,WAV_URL)) {
    Serial.println("URL setup failed"); return;
  }
  int code=http.GET();
  Serial.printf("HTTP status: %d\n",code);
  if(code==200) {
    String transfer=http.header("Transfer-Encoding"); transfer.toLowerCase();
    String encoding=http.header("Content-Encoding"); encoding.toLowerCase();
    if((transfer.length()&&transfer!="identity") ||
       (encoding.length()&&encoding!="identity")) {
      Serial.println("Compressed/chunked response unsupported");
    } else if(!wav(*http.getStreamPtr())) {
      Serial.println("Playback failed; check WAV format and wiring");
    }
  } else {
    Serial.println("Expected HTTP 200; check direct file URL");
  }
  http.end();
}
void setup() {
  Serial.begin(115200); delay(1000);
  pinMode(AUDIO_CS_PIN,OUTPUT); digitalWrite(AUDIO_CS_PIN,HIGH);
  pinMode(AUDIO_READY_PIN,INPUT_PULLDOWN);
  SPI.begin(AUDIO_SCK_PIN,-1,AUDIO_MOSI_PIN,AUDIO_CS_PIN);
  Serial.println("Network WAV fixed version");
  if(!strlen(WIFI_SSID)) {
    Serial.println("Fill config.h with Wi-Fi settings"); return;
  }
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  Serial.println("Connecting Wi-Fi (2.4GHz)...");
  uint32_t start=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-start<30000) delay(250);
  if(WiFi.status()!=WL_CONNECTED) {
    Serial.println("Wi-Fi failed; check settings and reset"); return;
  }
  Serial.print("Wi-Fi OK, IP: "); Serial.println(WiFi.localIP());
  Serial.println("Send p to play WAV; i for Wi-Fi status.");
}
void loop() {
  if(Serial.available()) {
    char c=Serial.read();
    if(c=='p') {
      if(WiFi.status()==WL_CONNECTED) playUrl();
      else Serial.println("Wi-Fi disconnected; reset to reconnect");
    }
    if(c=='i') {
      Serial.printf("Wi-Fi status: %d\n",(int)WiFi.status());
      Serial.println(WiFi.localIP());
    }
  }
  delay(1);
}
