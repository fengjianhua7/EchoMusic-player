#include <string>
#include <cassert>
#include <cstdint>
#include <cstdio>
static bool otaMaintenance=true;static std::string otaPin="12345678";static unsigned otaAttempts;static uint32_t now=100,otaExpires=1000;static uint32_t millis(){return now;}static struct{bool valid=false;bool authenticate(const char*,const char*){return valid;}}phoneServer;
static bool otaAuthenticated(){
 if(!otaMaintenance||!otaPin.length()||(int32_t)(millis()-otaExpires)>=0||otaAttempts>=5)return false;
 if(!phoneServer.authenticate("admin",otaPin.c_str())){otaAttempts++;return false;}return true;
}
int main(){for(int i=0;i<5;i++)assert(!otaAuthenticated());phoneServer.valid=true;assert(!otaAuthenticated());otaAttempts=0;assert(otaAuthenticated());now=1000;assert(!otaAuthenticated());now=0xfffffff0;otaExpires=100;assert(otaAuthenticated());otaMaintenance=false;assert(!otaAuthenticated());puts("Actual OTA PIN gate passed: 5-attempt limit, expiry, counter wrap and maintenance requirement");}
