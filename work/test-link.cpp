#include <string>
#include <deque>
#include <cassert>
#include <cstdio>
struct String{std::string s;String& operator+=(char c){s+=c;return *this;}String& operator=(const char* x){s=x;return *this;}size_t length()const{return s.size();}void trim(){size_t a=s.find_first_not_of(" \r\t");if(a==std::string::npos){s.clear();return;}size_t b=s.find_last_not_of(" \r\t");s=s.substr(a,b-a+1);}};
struct Link{std::deque<char> q;int available(){return (int)q.size();}char read(){char c=q.front();q.pop_front();return c;}void feed(const char* x){while(*x)q.push_back(*x++);}}screenLink;
static String boardLine;
static bool readLinkLine(String& result){
 while(screenLink.available()){
  char c=(char)screenLink.read();
  if(c=='\n'){boardLine.trim();result=boardLine;boardLine="";return true;}
  if(c!='\r'&&boardLine.length()<1100)boardLine+=c;
 }
 return false;
}

int main(){String out;screenLink.feed("UI:SE");assert(!readLinkLine(out));screenLink.feed("EK:500\r\nSD_BLO");assert(readLinkLine(out)&&out.s=="UI:SEEK:500");assert(!readLinkLine(out));screenLink.feed("CK:512:ABC\nUI_OK:32000\n");assert(readLinkLine(out)&&out.s=="SD_BLOCK:512:ABC");assert(readLinkLine(out)&&out.s=="UI_OK:32000");assert(!readLinkLine(out));puts("Shared UART fragmented/interleaved line tests passed");}
