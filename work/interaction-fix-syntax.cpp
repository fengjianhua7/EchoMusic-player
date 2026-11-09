#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include "config.h"
#include "audio_gain.h"
static bool saveMode=false,cardLocal=false;
static bool processCardLine(const String& line);
static void stopCardLocal();
static void requestCardStop();
static void requestCardNext();
static bool playCardMp3(const String& name,uint32_t size);
static bool waitReady(int level);
static bool mp3(Stream& in,int length);
#include "direct_music.h"
static bool uiFrame(const String& frame);
static void clearScreenRx();
static void onConsoleLogin();
#include "qr_login.h"
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"
static void inferDuration(const uint8_t* frame,unsigned size,const mp3dec_frame_info_t& info,int samples,int length);
SET_LOOP_TASK_STACK_SIZE(32768);

static const int AUDIO_SCK_PIN=12, AUDIO_MOSI_PIN=11, AUDIO_CS_PIN=10, AUDIO_READY_PIN=9;
static uint8_t packet[512];
static void servicePhoneUi();
static bool playbackGate();
static bool playUrl(const String& requestedUrl);
#include "console_state.h"
#include "player_controls.h"
static void appendCardState(cJSON* root);
static void registerCardRoutes();
#include "favorite_library.h"
#include "phone_ui.h"
static bool processCardLine(const String& line);
#include "card_storage.h"
#include "lyrics_player.h"
#include "touch_console.h"
#include "startup_player.h"
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
  if(saveMode){savedBytes+=downloadChunkBytes;savedCrc=cardCrc(savedCrc,packet,downloadChunkBytes);}
  bool ok=waitReady(LOW);
  digitalWrite(AUDIO_CS_PIN,HIGH); SPI.endTransaction();
  if(!ok) {
    Serial.println("ACK timeout. Playback stopped; reset STM32 if storage times out.");
    return false;
  }
  if(!saveMode)submittedSamples+=256;
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
static uint32_t be32(const uint8_t* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void inferDuration(const uint8_t* frame,unsigned size,const mp3dec_frame_info_t& info,int samples,int length){
 if(seekReadOffset)return;
 seekRecordFrame(&seekInfo,frame,size,frameAbsoluteOffset,(uint32_t)length,(unsigned)info.hz,(unsigned)samples,(unsigned)info.bitrate_kbps);
 if(!trackDurationMs||seekInfo.exactDuration){trackDurationMs=seekInfo.durationMs;durationEstimated=!seekInfo.exactDuration;}
}
static bool mp3(Stream& in, int length) {
  if(length<=0) {Serial.println("Content-Length required");return false;}
  AudioGain gain;audioGainInit(&gain,(unsigned)volumePercent);
  mp3dec_init(&decoder);
  size_t used=0, out=0;
  uint32_t left=(uint32_t)length, phase=0, frames=0;
  int lastHz=0;
  uint64_t sourceSamples=0,discardUntilSamples=0;
  bool inspected=false;
  while(left || used) {
    if(trackSeeking&&millis()-seekStartedAt>15000){Serial.println("Seek timeout (15 seconds)");playbackError="定位超时，请重试或重新播放";return false;}
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
    frameAbsoluteOffset=seekReadOffset+(uint32_t)length-left-(uint32_t)used+(uint32_t)info.frame_offset;
    if(samples>0&&info.hz>0)inferDuration(compressed+info.frame_offset,(unsigned)(info.frame_bytes-info.frame_offset),info,samples,length);
    used-=info.frame_bytes;
    memmove(compressed,compressed+info.frame_bytes,used);
    if(samples>0) {
      if(info.hz<=0 || (info.channels!=1 && info.channels!=2)) return false;
      if(lastHz && lastHz!=info.hz) {Serial.println("Changing sample rate unsupported");return false;}
      if(!lastHz){
        Serial.printf("MP3: %d Hz, %d channels\n",info.hz,info.channels);
        if(!saveMode&&trackBaseMs>seekDecodeBaseMs)discardUntilSamples=((uint64_t)(trackBaseMs-seekDecodeBaseMs)*(unsigned)info.hz+999)/1000;
      }
      lastHz=info.hz; frames++;
      int firstSample=0;
      if(sourceSamples<discardUntilSamples){uint64_t remaining=discardUntilSamples-sourceSamples;firstSample=remaining>=(unsigned)samples?samples:(int)remaining;}
      sourceSamples+=(unsigned)samples;
      if(firstSample==samples)continue;
      trackSeeking=false;
      for(int i=firstSample;i<samples;i++) {
        int32_t value=pcm[i*info.channels];
        if(info.channels==2) value=(value+pcm[i*2+1])/2;
        int16_t v=(int16_t)(saveMode?value:audioGainApply(&gain,value,(unsigned)volumePercent));
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
  if(frames)finishAudio();
  Serial.printf("Decoded frames: %lu\n",(unsigned long)frames);
  return frames>0;
}
static bool playUrl(const String& requestedUrl) {
  bool secure=requestedUrl.startsWith("https://");
  if(!secure && !requestedUrl.startsWith("http://")) return false;
  NetworkClient plain;
  NetworkClientSecure tls; tls.useBuiltinCACertBundle();tls.setHandshakeTimeout(seekReadOffset?8:20);
  HTTPClient http;
  http.useHTTP10(seekReadOffset?false:true);
  http.setConnectTimeout(seekReadOffset?8000:15000);
  http.setTimeout(seekReadOffset?8000:10000);
  const char* keys[]={"Transfer-Encoding","Content-Encoding","Content-Type","Content-Range"};
  http.collectHeaders(keys,4);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  NetworkClient& client=secure?static_cast<NetworkClient&>(tls):plain;
  if(!http.begin(client,requestedUrl)) {
    Serial.println("URL setup failed"); return false;
  }
  bool playbackOk=false;
  if(seekReadOffset)http.addHeader("Range","bytes="+String(seekReadOffset)+"-");
  int code=http.GET();
  Serial.printf("HTTP status: %d\n",code);
  if(seekReadOffset&&code!=206){Serial.println("Seek: server did not accept byte Range");playbackError="服务器不支持快速定位，请重新播放";http.end();return false;}
  if(code==200 || code==206) {
    if(seekReadOffset){String range=http.header("Content-Range");if(!range.startsWith("bytes "+String(seekReadOffset)+"-")){Serial.println("Seek: invalid Content-Range");http.end();return false;}}

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
 Serial.begin(115200);delay(500);screenLink.begin(230400,SERIAL_8N1,18,17);
 pinMode(AUDIO_CS_PIN,OUTPUT);digitalWrite(AUDIO_CS_PIN,HIGH);pinMode(AUDIO_READY_PIN,INPUT_PULLDOWN);
 SPI.begin(AUDIO_SCK_PIN,13,AUDIO_MOSI_PIN,AUDIO_CS_PIN);
 Serial.println("Player interaction fixes v25.0");
 Serial.printf("PSRAM total=%u free=%u\n",ESP.getPsramSize(),ESP.getFreePsram());
 if(awaitScreenReady(180000))startPlayerAfterScreen();
}
static String command;
void loop() {
  if(!screenReady){if(awaitScreenReady(2000))startPlayerAfterScreen();return;}
  serviceNetworkStartup();
  while(Serial.available()) {
    char c=(char)Serial.read();
    if(c=='\n'||c=='\r') {
      command.trim();
      if(command=="qrtest")showQr("https://www.kugou.com/");
      else if(command=="login")beginQrLogin();
      else if(command=="logout")logoutMusic();
      else if(command.startsWith("s ")) {
        if(WiFi.status()==WL_CONNECTED){browseSource=SOURCE_ONLINE;activeSource=SOURCE_ONLINE;browsePage=0;searchMusic(command.substring(2));syncSongList();}
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
  serviceConsole();
  processConsoleActions();
  processFavorites();
  processPhoneSearch();
  processCardRequest();
  dispatchBoardCommand();
  pollQrLogin();
  delay(1);
}
