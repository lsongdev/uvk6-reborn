#include "core/watch.h"
#include <assert.h>
static uint32_t now;
static uint8_t present, readFails, prepareFails;
static unsigned prepares[2], selects, unexpectedTunes;
static bool audible;
static uint8_t owner;
static bool tune(void *x,const WatchTarget *t) { (void)x;(void)t;++unexpectedTunes;return false; }
static bool prepare(void *x,const WatchTarget *t) {
  (void)x;++prepares[t->id];return !(prepareFails&(1u<<t->id));
}
static bool signal(void *x,const RxPlan *p,bool *found) {
  (void)x;uint8_t id=p->device==RX_DEVICE_SI ? 0 : 1;
  *found=(present&(1u<<id))!=0;return !(readFails&(1u<<id));
}
static bool selectAudio(void *x,const WatchTarget *t,bool open) {
  (void)x;++selects;owner=t->id;audible=open;return true;
}
static void audio(void *x,bool open) { (void)x;(void)open;assert(0); }
static const WatchOperations ops={.tune=tune,.prepare=prepare,.signal=signal,.select=selectAudio,.audio=audio};
static const WatchTarget pair[]={
  {0,{RX_DEVICE_SI,95900000,{RX_WFM,100000,0,0,0}}},
  {1,{RX_DEVICE_BK,438500000,{RX_NFM,12500,12000,4,0}}}
};
static void tick(WatchRuntime *r,unsigned ms) { now+=ms;WATCH_Update(r,now,&ops); }
static void settle(WatchRuntime *r) { for(unsigned i=0;i<20;++i) tick(r,50); }
int main(void) {
  WatchRuntime r={0};present=3;
  assert(WATCH_Start(&r,pair,2,0) && r.parallel);settle(&r);
  assert(audible && owner==0 && r.signal_mask==3 && !unexpectedTunes);
  unsigned before=selects;
  for(unsigned i=0;i<100;++i) tick(&r,50);
  assert(selects==before && prepares[0]==1 && prepares[1]==1 && audible && owner==0);
  // Priority A disappears; hold silent for a second, then play B.
  present=2;tick(&r,50);assert(!audible && r.phase==WATCH_HANG);
  tick(&r,WATCH_HANG_MS-50);assert(!audible);tick(&r,50);
  assert(audible && owner==1);
  // A returns and takes audio after confirmation, without preparing again.
  present=3;tick(&r,50);assert(owner==1);tick(&r,100);
  assert(owner==0 && audible && prepares[0]==1 && prepares[1]==1);
  // Read failure mutes the affected audio and retries only that chip.
  readFails=1;tick(&r,50);assert(!audible);
  tick(&r,WATCH_HANG_MS);assert(audible && owner==1);
  unsigned beforeB=prepares[1];readFails=0;
  tick(&r,WATCH_RETRY_MS);settle(&r);
  assert(owner==0 && audible && prepares[0]==2 && prepares[1]==beforeB);
  // Priority B can take a continuously playing A without cutting A to probe.
  WATCH_Stop(&r);present=1;prepares[0]=prepares[1]=0;
  assert(WATCH_Start(&r,pair,2,1));settle(&r);assert(owner==0 && audible);
  before=selects;tick(&r,10000);assert(owner==0 && audible && selects==before);
  present=3;tick(&r,50);tick(&r,100);assert(owner==1 && audible);
  before=selects;tick(&r,10000);assert(owner==1 && audible && selects==before);
  // One failed setup does not interrupt the other chip; retry stays bounded.
  WATCH_Stop(&r);present=1;prepareFails=2;prepares[0]=prepares[1]=0;
  assert(WATCH_Start(&r,pair,2,0));settle(&r);assert(owner==0 && audible);
  assert(prepares[1]==1);prepareFails=0;tick(&r,WATCH_RETRY_MS);settle(&r);
  assert(prepares[0]==1 && prepares[1]==2 && owner==0 && audible);
  // Parallel confirmation deadlines are safe across timer rollover.
  WATCH_Stop(&r);now=UINT32_MAX-200;present=3;
  assert(WATCH_Start(&r,pair,2,1));settle(&r);assert(owner==1 && audible);
  assert(!unexpectedTunes);
  WATCH_Stop(&r);now=0x90000000u;
  assert(WATCH_Start(&r,pair,2,0));settle(&r);assert(owner==0 && audible);
  return 0;
}
