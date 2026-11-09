#include <string>
#include <algorithm>
#include <cassert>
#include <cstdio>
using std::max;
struct String:std::string{String()=default;using std::string::string;using std::string::operator=;String(int n):std::string(std::to_string(n)){}String(const std::string& x):std::string(x){}};
enum{SOURCE_ONLINE,SOURCE_FAVORITE,SOURCE_CARD};
static bool autoPlay=true,seekRestart=false,favoriteHasNext=true;static unsigned seekTargetMs;
static int playMode,activeSource,cardCount=5,cardIndex=2,localFilteredCount=0,localFiltered[128],favoriteCount=8,favoritePage=1,selectedSong,queuedOnlinePlay=-1,favoriteRequestPage,favoritePlay=-1,musicCount=16;
static String cardRequest,localFilter;
static void queueFollowing(bool explicitNext,int direction){
 if(!explicitNext&&(!autoPlay||playMode==2))return;
 bool repeat=!explicitNext&&playMode==1;
 seekRestart=false;seekTargetMs=0;
 if(activeSource==SOURCE_CARD){
  if(cardCount){int i=repeat?cardIndex:(cardIndex+direction+cardCount)%cardCount;
   if(!repeat&&localFilter.length()&&localFilteredCount){int at=-1;for(int k=0;k<localFilteredCount;k++)if(localFiltered[k]==cardIndex)at=k;i=at<0?localFiltered[direction>0?0:localFilteredCount-1]:localFiltered[(at+direction+localFilteredCount)%localFilteredCount];}
   cardRequest="play:"+String(i);}
 }else if(activeSource==SOURCE_FAVORITE){
  int next=repeat?selectedSong:selectedSong+direction;
  if(next>=favoriteCount){favoriteRequestPage=favoriteHasNext?favoritePage+1:1;favoritePlay=0;}
  else if(next<0){if(favoritePage>1){favoriteRequestPage=favoritePage-1;favoritePlay=7;}else favoritePlay=max(0,favoriteCount-1);}
  else queuedOnlinePlay=next;
 }else if(musicCount){queuedOnlinePlay=repeat?selectedSong:(selectedSong+direction+musicCount)%musicCount;}
}

int main(){activeSource=SOURCE_CARD;queueFollowing(false,1);assert(cardRequest=="play:3");playMode=1;queueFollowing(false,1);assert(cardRequest=="play:2");playMode=2;cardRequest="";queueFollowing(false,1);assert(cardRequest.empty());queueFollowing(true,1);assert(cardRequest=="play:3");cardIndex=0;queueFollowing(true,-1);assert(cardRequest=="play:4");
 playMode=0;localFilter="x";localFilteredCount=2;localFiltered[0]=1;localFiltered[1]=3;cardIndex=1;queueFollowing(false,1);assert(cardRequest=="play:3");
 activeSource=SOURCE_ONLINE;selectedSong=15;queueFollowing(false,1);assert(queuedOnlinePlay==0);playMode=1;selectedSong=3;queueFollowing(false,1);assert(queuedOnlinePlay==3);
 activeSource=SOURCE_FAVORITE;playMode=0;selectedSong=7;queueFollowing(false,1);assert(favoriteRequestPage==2&&favoritePlay==0);
 favoritePage=2;selectedSong=0;queueFollowing(true,-1);assert(favoriteRequestPage==1&&favoritePlay==7);
 puts("Automatic next/repeat/stop, filtered order and cloud page transition tests passed");}
