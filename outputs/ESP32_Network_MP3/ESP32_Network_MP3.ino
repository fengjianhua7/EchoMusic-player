#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include "config.h"
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"
SET_LOOP_TASK_STACK_SIZE(32768);

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
static mp3dec_t decoder;
static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static uint8_t compressed[16384];
static bool mp3(NetworkClient& in, int length) {
  if(length<=0) {Serial.println("Content-Length required");return false;}
  mp3dec_init(&decoder);
  size_t used=0, out=0;
  uint32_t left=(uint32_t)length, phase=0, frames=0;
  int lastHz=0;
  while(left || used) {
    if(left && used<8192) {
      size_t n=min((size_t)left,sizeof(compressed)-used);
      if(!exact(in,compressed+used,n)) return false;
      used+=n; left-=n;
    }
    mp3dec_frame_info_t info={};
    int samples=mp3dec_decode_frame(&decoder,compressed,(int)used,pcm,&info);
    if(info.frame_bytes<=0) {
      if(left && used<sizeof(compressed)) {
        size_t n=min((size_t)left,sizeof(compressed)-used);
        if(!exact(in,compressed+used,n)) return false;
        used+=n; left-=n; continue;
      }
      break;
    }
    if((size_t)info.frame_bytes>used) return false;
    used-=info.frame_bytes;
    memmove(compressed,compressed+info.frame_bytes,used);
    if(samples>0) {
      if(info.hz<=0 || (info.channels!=1 && info.channels!=2)) return false;
      if(lastHz && lastHz!=info.hz) {Serial.println("Changing sample rate unsupported");return false;}
      if(!lastHz) Serial.printf("MP3: %d Hz, %d channels\n",info.hz,info.channels);
      lastHz=info.hz; frames++;
      for(int i=0;i<samples;i++) {
        int32_t value=pcm[i*info.channels];
        if(info.channels==2) value=(value+pcm[i*2+1])/2;
        int16_t v=(int16_t)(value/4);
        // Basic rate conversion for bring-up; upgrade with low-pass filtering later.
        phase+=32000;
        while(phase>=(uint32_t)info.hz) {
          phase-=info.hz;
          packet[out++]=(uint8_t)v;
          packet[out++]=(uint8_t)((uint16_t)v>>8);
          if(out==512) {if(!sendBlock()) return false;out=0;}
        }
      }
    }
    delay(0);
  }
  if(out) {memset(packet+out,0,512-out);if(!sendBlock()) return false;}
  Serial.printf("Decoded frames: %lu\n",(unsigned long)frames);
  return frames>0;
}
static void playUrl() {
  if(strncmp(MP3_URL,"http://",7)) {
    Serial.println("Set MP3_URL to an HTTP MP3 link first"); return;
  }
  NetworkClient client;
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(10000);
  const char* keys[]={"Transfer-Encoding","Content-Encoding"};
  http.collectHeaders(keys,2);
  if(!http.begin(client,MP3_URL)) {
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
    } else if(!mp3(*http.getStreamPtr(),http.getSize())) {
      Serial.println("Playback failed; check MP3 format and wiring");
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
  Serial.println("Network MP3 version");
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
  Serial.println("Send p to play MP3; i for Wi-Fi status.");
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
