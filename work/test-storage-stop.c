#include <assert.h>
#include <stdio.h>
static int media,mode=3,paused=1,fileTransfer=1,closed,cleared,canceled;
int f_close(int* f){closed++;return 0;}void Audio_Clear(void){cleared++;}void Audio_CancelTransfer(void){canceled++;}
static void stop(void){if(mode)f_close(&media);mode=0;paused=0;fileTransfer=0;Audio_Clear();Audio_CancelTransfer();}
int main(void){stop();assert(!mode&&!paused&&!fileTransfer&&closed==1&&cleared==1&&canceled==1);stop();assert(closed==1&&cleared==2&&canceled==2);puts("Storage stop recovery test passed: closes stale file and clears audio/partial SPI transaction, including already-idle state");}
