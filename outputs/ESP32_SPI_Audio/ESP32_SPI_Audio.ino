#include <SPI.h>
#include <math.h>
// ESP32-S3 GPIO numbers, not header positions.
static const int SCK_PIN=12, MOSI_PIN=11, CS_PIN=10, READY_PIN=9;
static uint8_t packet[512];
static uint32_t phase=0;
static bool waitLevel(int level) {
  uint32_t start=millis();
  while(digitalRead(READY_PIN)!=level) {
    if(millis()-start>3000) return false;
    delay(1);
  }
  return true;
}
void setup() {
  Serial.begin(115200);
  pinMode(CS_PIN,OUTPUT); digitalWrite(CS_PIN,HIGH);
  pinMode(READY_PIN,INPUT_PULLDOWN);
  SPI.begin(SCK_PIN,-1,MOSI_PIN,CS_PIN);
  Serial.println("SPI tone test. Reset STM32 first if transfer stalls.");
}
void loop() {
  // 32 kHz mono signed PCM16 little endian; 1 kHz, low amplitude.
  for(int i=0;i<256;i++) {
    int16_t v=(int16_t)(4096.0f*sinf(2.0f*PI*(phase%32)/32.0f));
    phase++;
    packet[2*i]=(uint8_t)v;
    packet[2*i+1]=(uint8_t)((uint16_t)v>>8);
  }
  if(!waitLevel(HIGH)) { Serial.println("Waiting for STM32 READY"); return; }
  SPI.beginTransaction(SPISettings(1000000,MSBFIRST,SPI_MODE0));
  digitalWrite(CS_PIN,LOW);
  SPI.transferBytes(packet,nullptr,sizeof(packet));
  // Keep CS low until receiver acknowledges. This prevents missing a short pulse.
  bool ack=waitLevel(LOW);
  digitalWrite(CS_PIN,HIGH);
  SPI.endTransaction();
  if(!ack) {
    Serial.println("ACK timeout: reset STM32 and ESP32");
    while(true) delay(1000);
  }
}
