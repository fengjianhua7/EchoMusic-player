#pragma once
enum {SOURCE_ONLINE=0,SOURCE_FAVORITE=1,SOURCE_CARD=2};
static int browseSource=0,activeSource=0,browsePage=0;
static int playMode=0; // 0 sequential, 1 repeat one, 2 stop after one
static bool consoleFinishedEvent=false;
static bool autoPlay=true,trackSeeking=false,seekRestart=false;
static uint32_t trackPositionMs=0,trackDurationMs=0,trackBaseMs=0,seekTargetMs=0;
static uint32_t submittedSamples=0,lastProgressAt=0;
static bool durationEstimated=false;
static String consoleAction,consoleTitle;
static int queuedOnlinePlay=-1;
static void serviceConsole();
static void consoleProgress(bool force=false);
static void consoleBegin(const String& title,uint32_t duration,bool estimated=false);
static void consoleFinish(bool success);
static bool handleTouchCommand(const String& line);
static void syncConsoleList();
static void queueFollowing(bool explicitNext=false,int direction=1);
static void prepareNetworkLyrics(int index);
static void prepareLocalLyrics(int index);
static void clearLyrics();
static void receiveLocalLyric(const String& line);
static void lyricProgress();
static bool cardCommand(const String& cmd,const String& expected,uint32_t timeout=8000);
static void finishAudio();
static void processConsoleActions();

static void replayLocal();

static void consoleMessage(const String& text);
