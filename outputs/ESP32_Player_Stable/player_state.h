#pragma once
#include <Preferences.h>
static int volumePercent=25;
static int playMode=0; // 0 single, 1 list loop, 2 repeat one, 3 shuffle
static String statusMessage="Starting";
static String rememberedQuery;
static unsigned listRevision=0;
static char commands[16];
static unsigned commandHead=0,commandTail=0,commandCount=0;
static bool settingsDirty=false;
static uint32_t settingsChanged=0;
static bool enqueueCommand(char command) {
 if(!command)return true;
 if(commandCount==16)return false;
 commands[commandTail]=command;commandTail=(commandTail+1)%16;commandCount++;return true;
}
static char dequeueCommand() {
 if(!commandCount)return 0;
 char c=commands[commandHead];commandHead=(commandHead+1)%16;commandCount--;return c;
}
static void clearCommands(){commandHead=commandTail=commandCount=0;}
static void markSettings(){settingsDirty=true;settingsChanged=millis();}
static void setVolume(int value){volumePercent=constrain(value,0,100);markSettings();}
static void loadPlayerSettings() {
 Preferences p;if(!p.begin("player-ui",true))return;
 volumePercent=constrain((int)p.getUChar("volume",25),0,100);
 playMode=constrain((int)p.getUChar("mode",0),0,3);
 rememberedQuery=p.getString("query","");p.end();
}
static void savePlayerSettings(bool audioBusy) {
 if(!settingsDirty || audioBusy || millis()-settingsChanged<3000)return;
 Preferences p;if(!p.begin("player-ui",false))return;
 bool ok=p.putUChar("volume",(uint8_t)volumePercent)>0;
 ok=(p.putUChar("mode",(uint8_t)playMode)>0)&&ok;
 if(rememberedQuery.length())ok=(p.putString("query",rememberedQuery)>0)&&ok;
 p.end();if(ok)settingsDirty=false;else settingsChanged=millis();
}
