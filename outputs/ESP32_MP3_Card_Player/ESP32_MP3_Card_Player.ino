#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include "config.h"
static bool saveMode=false,cardLocal=false;
static bool processCardLine(const String& line);
static void stopCardLocal();
static void requestCardStop();
static void requestCardNext();
static bool playCardMp3(const String& name,uint32_t size);
static bool waitReady(int level);
static bool mp3(Stream& in,int length);
#include "direct_music.h"
#include "qr_login.h"
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"
SET_LOOP_TASK_STACK_SIZE(32768);

static const int AUDIO_SCK_PIN=12, AUDIO_MOSI_PIN=11, AUDIO_CS_PIN=10, AUDIO_READY_PIN=9;
static uint8_t packet[512];
static void servicePhoneUi();
static bool playbackGate();
static bool playUrl(const String& requestedUrl);
#include "player_controls.h"
static void appendCardState(cJSON* root);
static void registerCardRoutes();
#include "phone_ui.h"
static bool processCardLine(const String& line);
#include "card_storage.h"
static bool waitReady(int level) {
  uint32_t start=millis();
  while(digitalRead(AUDIO_READY_PIN)!=level) {
    if(millis()-start>3000) return false;
    delay(0);
  }
  return true;
}
static bool sendBlock() {
  if(!playbackGate()){Serial.println("Playback interrupted by control or network change");return false;}
  if(!waitReady(HIGH)) { Serial.println("STM32 AUDIO_READY_PIN timeout"); return false; }
  SPI.beginTransaction(SPISettings(2000000,MSBFIRST,SPI_MODE0));
  digitalWrite(AUDIO_CS_PIN,LOW);
  SPI.transferBytes(packet,nullptr,512);
  if(saveMode){savedBytes+=512;savedCrc=cardCrc(savedCrc,packet,512);}
  bool ok=waitReady(LOW);
  digitalWrite(AUDIO_CS_PIN,HIGH); SPI.endTransaction();
  if(!ok) {
    Serial.println("ACK timeout. Playback stopped; reset STM32 if storage times out.");
    return false;
  }
  return true;
}
static bool exact(Stream& in,uint8_t* dest,size_t count) {
  uint32_t last=millis();
  while(count) {
    if(!playbackGate()){Serial.println("Playback interrupted by control or network change");return false;}
    int n=in.available();
    if(n>0) {
      size_t want=min(count,(size_t)n);
      int got=(int)in.readBytes((char*)dest,want);
      if(got>0) {dest+=got; count-=got; last=millis();}
    } else {
      if(millis()-last>10000) {
        Serial.printf("Audio read failed: connected=%d, still-needed=%u, heap=%u\n",WiFi.status()==WL_CONNECTED,(unsigned)count,ESP.getFreeHeap());return false;
      }
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
static uint8_t compressed[8192];
static bool mp3(Stream& in, int length) {
  if(length<=0) {Serial.println("Content-Length required");return false;}
  mp3dec_init(&decoder);
  size_t used=0, out=0;
  uint32_t left=(uint32_t)length, phase=0, frames=0;
  int lastHz=0;
  bool inspected=false;
  while(left || used) {
    while(left && used<4096) {
      size_t n=min((size_t)left,min((size_t)1024,sizeof(compressed)-used));
      if(!exact(in,compressed+used,n)) return false;
      used+=n; left-=n;
    }
    if(!inspected && used>=4) {
      inspected=true;
      Serial.print("Audio first bytes: ");
      for(size_t i=0;i<used && i<16;i++)Serial.printf("%02X ",(unsigned)compressed[i]);
      Serial.println();
      if(!memcmp(compressed,"OggS",4)) {Serial.println("Received OGG, not MP3; decoder unsupported");return false;}
      if(!memcmp(compressed,"fLaC",4)) {Serial.println("Received FLAC, not MP3; decoder unsupported");return false;}
      if(!memcmp(compressed,"RIFF",4)) {Serial.println("Received WAV, not MP3");return false;}
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
        int16_t v=(int16_t)(saveMode?value:((value*volumePercent)/100));
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
static bool playUrl(const String& requestedUrl) {
  bool secure=requestedUrl.startsWith("https://");
  if(!secure && !requestedUrl.startsWith("http://")) return false;
  NetworkClient plain;
  NetworkClientSecure tls; tls.setCACert(MUSIC_ROOT_CA);
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(10000);
  const char* keys[]={"Transfer-Encoding","Content-Encoding","Content-Type"};
  http.collectHeaders(keys,3);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  NetworkClient& client=secure?static_cast<NetworkClient&>(tls):plain;
  if(!http.begin(client,requestedUrl)) {
    Serial.println("URL setup failed"); return false;
  }
  bool playbackOk=false;
  int code=http.GET();
  Serial.printf("HTTP status: %d\n",code);
  if(code==200) {
    Serial.print("Audio Content-Type: ");Serial.println(http.header("Content-Type"));
    Serial.printf("Audio Content-Length: %d\n",http.getSize());
    String transfer=http.header("Transfer-Encoding"); transfer.toLowerCase();
    String encoding=http.header("Content-Encoding"); encoding.toLowerCase();
    if((transfer.length()&&transfer!="identity") ||
       (encoding.length()&&encoding!="identity")) {
      Serial.println("Compressed/chunked response unsupported");
    } else if(!(playbackOk=mp3(*http.getStreamPtr(),http.getSize()))) {
      Serial.println("Playback failed; inspect audio format and download logs");
    }
  } else {
    Serial.println("Expected HTTP 200; check direct file URL");
  }
  http.end();
  return playbackOk;
}
void setup() {
  Serial.begin(115200); delay(1000);
  screenLink.begin(230400,SERIAL_8N1,18,17);
  pinMode(AUDIO_CS_PIN,OUTPUT); digitalWrite(AUDIO_CS_PIN,HIGH);
  pinMode(AUDIO_READY_PIN,INPUT_PULLDOWN);
  SPI.begin(AUDIO_SCK_PIN,13,AUDIO_MOSI_PIN,AUDIO_CS_PIN);
  Serial.println("Player MP3 buffered transport v20.9");
  Serial.printf("PSRAM total=%u, free=%u\n",ESP.getPsramSize(),ESP.getFreePsram());
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
  initMusicIdentity();
  beginPhoneUi();
  configTime(0,0,"pool.ntp.org","time.cloudflare.com","ntp.aliyun.com");
  uint32_t syncStart=millis();
  while(time(nullptr)<1700000000 && millis()-syncStart<20000) delay(250);
  Serial.println("Commands: login | qrtest | logout | s keyword | 1..5 play | p test MP3 | i status");
  if(sessionToken.length())searchPreset();
  else {screenStatus("Hold K2 for QR login");}
}
static String command;
void loop() {
  while(Serial.available()) {
    char c=(char)Serial.read();
    if(c=='\n'||c=='\r') {
      command.trim();
      if(command=="qrtest")showQr("https://www.kugou.com/");
      else if(command=="login")beginQrLogin();
      else if(command=="logout")logoutMusic();
      else if(command.startsWith("s ")) {
        if(WiFi.status()==WL_CONNECTED){searchMusic(command.substring(2));syncSongList();}
      } else if(command.length()==1 && command[0]>='1' && command[0]<='5') {
        playSelectedSong(command[0]-'1');
      } else if(command=="p")playUrl(MP3_URL);
      else if(command=="i") {
        Serial.print("IP: ");Serial.println(WiFi.localIP());
        Serial.printf("Clock synced: %s, free heap: %u\n",time(nullptr)>=1700000000?"yes":"no",ESP.getFreeHeap());
      }
      command="";
    } else if(command.length()<256)command+=c;
  }
  servicePhoneUi();
  processPhoneSearch();
  processCardRequest();
  dispatchBoardCommand();
  pollQrLogin();
  delay(1);
}
