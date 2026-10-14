// Multi-slot watch: background vs events, first come first served, hang.
#include "core/watch.h"
#include <assert.h>

static uint32_t now;
static unsigned prepares, samples, selects;
static bool talking[RADIO_SLOT_CAPACITY], prepareOK=true, signalOK=true, selectOK=true;
static uint8_t bkTuned=UINT8_MAX, heard=UINT8_MAX; // UINT8_MAX: speaker silent.
static uint32_t prepareLatency;
static bool prepare(void *unused, const WatchTarget *t) {
  (void)unused; ++prepares; now+=prepareLatency;
  if (!prepareOK) return false;
  if (t->plan.device==RX_DEVICE_BK) bkTuned=t->id;
  return true;
}
static bool signal(void *unused, const RxPlan *plan, bool *found) {
  (void)unused; ++samples;
  assert(plan->device==RX_DEVICE_BK); // Only events are sampled.
  *found=talking[bkTuned];
  return signalOK;
}
static bool select(void *unused, const WatchTarget *t, bool open) {
  (void)unused; ++selects;
  if (!selectOK) return false;
  heard=open ? t->id : UINT8_MAX;
  return true;
}
static uint32_t clockNow(void *unused) { (void)unused; return now; }
static const WatchOperations ops={.prepare=prepare,.signal=signal,.select=select,.now=clockNow};
static void tick(WatchRuntime *r, uint32_t elapsed) { now+=elapsed; WATCH_Update(r,now,&ops); }
static void run(WatchRuntime *r, uint32_t ms) { for (uint32_t t=0; t<ms; t+=10) tick(r,10); }
static void reset(void) {
  now=1000; prepares=samples=selects=0; prepareLatency=0;
  prepareOK=signalOK=selectOK=true; bkTuned=heard=UINT8_MAX;
  for (unsigned i=0;i<RADIO_SLOT_CAPACITY;++i) talking[i]=false;
}
static WatchTarget bk(uint8_t id, uint32_t hz, uint8_t squelch) {
  return (WatchTarget){id,{RX_DEVICE_BK,hz,{RX_NFM,12500,12000,squelch,0}}};
}
static WatchTarget si(uint8_t id, uint32_t hz) {
  return (WatchTarget){id,{RX_DEVICE_SI,hz,{RX_WFM,100000,0,0,0}}};
}

int main(void) {
  WatchRuntime r={0};
  WatchTarget a=bk(0,145500000,4), b=bk(1,438500000,4), c=bk(2,446000000,4);
  WatchTarget fm=si(3,95900000), fm2=si(1,103900000);

  // Start: at least two distinct sources; duplicates, SSB and extra
  // backgrounds collapse.
  WatchTarget pair[]={a,b};
  assert(!WATCH_Start(&r,pair,1,UINT8_MAX,now));
  WatchTarget dup[]={a,a};dup[1].id=1;
  assert(!WATCH_Start(&r,dup,2,UINT8_MAX,now));
  dup[1].plan.parameters.step_hz=25000;
  assert(!WATCH_Start(&r,dup,2,UINT8_MAX,now)); // Step is not a distinct receive path.
  dup[1].plan.parameters.rx_tone=1;
  assert(WATCH_Start(&r,dup,2,UINT8_MAX,now)); // A different tone is.
  dup[1].plan.parameters.mode=RX_USB;
  assert(!WATCH_Start(&r,dup,2,UINT8_MAX,now));
  WatchTarget radios[]={fm,fm2};
  assert(!WATCH_Start(&r,radios,2,UINT8_MAX,now)); // Only one background plays.
  WatchTarget four[]={a,b,c,fm};
  assert(WATCH_Start(&r,four,4,UINT8_MAX,now) && r.count==3 && r.has_background);
  assert(r.devices==((1u<<RX_DEVICE_BK)|(1u<<RX_DEVICE_SI)));
  // The selected background wins; otherwise the first one.
  WatchTarget open0=bk(0,145500000,0);
  WatchTarget bgs[]={open0,fm};
  assert(WATCH_Start(&r,bgs,2,UINT8_MAX,now)==false); // Two backgrounds, no event.
  WatchTarget mix[]={open0,b,fm};
  assert(WATCH_Start(&r,mix,3,UINT8_MAX,now) && r.background.id==0 && r.count==1);
  assert(WATCH_Start(&r,mix,3,3,now) && r.background.id==3);
  // Polling starts at the selected event.
  assert(WATCH_Start(&r,four,4,1,now) && r.events[r.cursor].id==1);

  // Events only: silent while idle, polled in turn.
  reset();
  WatchTarget trio[]={a,b,c};
  assert(WATCH_Start(&r,trio,3,UINT8_MAX,now));
  prepareLatency=20; tick(&r,0);
  assert(bkTuned==0 && r.phase==WATCH_SETTLE && WATCH_Probe(&r)==0 && WATCH_IsProbing(&r));
  tick(&r,WATCH_SETTLE_MS-1); assert(!samples); // Settle starts after the blocking tune.
  tick(&r,1); assert(samples==1 && r.phase==WATCH_POLL);
  tick(&r,0); assert(bkTuned==1);
  // B talks: verify, then B owns the speaker.
  talking[1]=true; prepareLatency=0;
  tick(&r,WATCH_SETTLE_MS); assert(r.phase==WATCH_VERIFY && heard==UINT8_MAX);
  tick(&r,WATCH_VERIFY_MS); assert(r.phase==WATCH_HOLD && heard==1 && WATCH_Owner(&r)==1);
  assert(!WATCH_IsProbing(&r) && WATCH_Probe(&r)==UINT8_MAX);
  // No time limit, and C talking meanwhile never interrupts B.
  talking[2]=true; unsigned tunes=prepares;
  run(&r,20000); assert(heard==1 && prepares==tunes);
  // B pauses: the channel is held (muted) for a reply.
  talking[1]=false; run(&r,WATCH_SAMPLE_MS);
  assert(r.phase==WATCH_HANG && heard==UINT8_MAX);
  run(&r,WATCH_HANG_MS-200); assert(r.phase==WATCH_HANG && prepares==tunes);
  talking[1]=true; run(&r,WATCH_SAMPLE_MS); // The reply comes back on B.
  assert(r.phase==WATCH_HOLD && heard==1);
  talking[1]=false; run(&r,WATCH_SAMPLE_MS+WATCH_HANG_MS);
  // After the hang polling moves on, to C, which is talking.
  run(&r,WATCH_SETTLE_MS+WATCH_VERIFY_MS+20);
  assert(heard==2 && WATCH_Owner(&r)==2);

  // Broadcast background: brought up once, plays when nobody talks,
  // yields to a talk, and returns after the hang.
  reset();
  WatchTarget withFm[]={a,fm};
  assert(WATCH_Start(&r,withFm,2,3,now));
  tick(&r,0); assert(r.background_ready && heard==3 && WATCH_Owner(&r)==3 && !WATCH_IsProbing(&r));
  tick(&r,0); assert(bkTuned==0 && heard==3); // BK polls muted under the broadcast.
  talking[0]=true; run(&r,WATCH_SETTLE_MS+WATCH_VERIFY_MS+10);
  assert(heard==0 && WATCH_Owner(&r)==0); // An event beats the selected background.
  talking[0]=false; run(&r,WATCH_SAMPLE_MS);
  assert(r.phase==WATCH_HANG && heard==UINT8_MAX); // Quiet reply window.
  run(&r,WATCH_HANG_MS); assert(heard==3);
  // FC borrows BK: a talk hands the speaker to the broadcast, BK slots
  // stop being polled, and resuming starts the rounds over.
  talking[0]=true; run(&r,WATCH_SETTLE_MS+WATCH_VERIFY_MS+WATCH_SAMPLE_MS);
  assert(heard==0);
  WATCH_SuspendBk(&r,true,now,&ops);
  assert(r.bk_suspended && heard==3 && WATCH_Owner(&r)==3);
  unsigned frozen=prepares+samples; run(&r,5000); assert(prepares+samples==frozen && heard==3);
  WATCH_SuspendBk(&r,false,now,&ops);
  run(&r,WATCH_SETTLE_MS+WATCH_VERIFY_MS+20); assert(heard==0); // A is still talking.
  talking[0]=false;
  // A failed broadcast retries later; events keep working meanwhile.
  reset(); prepareOK=false;
  assert(WATCH_Start(&r,withFm,2,UINT8_MAX,now));
  tick(&r,0); assert(!r.background_ready && heard==UINT8_MAX);
  prepareOK=true; tick(&r,10); assert(bkTuned==0 && !r.background_ready);
  run(&r,WATCH_RETRY_MS); assert(r.background_ready && heard==3);

  // BK background (squelch 0): heard between rounds of event checks.
  reset();
  WatchTarget sharedBk[]={open0,b,c};
  assert(WATCH_Start(&r,sharedBk,3,UINT8_MAX,now) && r.count==2 && r.devices==(1u<<RX_DEVICE_BK));
  tick(&r,0); assert(bkTuned==1 && heard==UINT8_MAX); // A first round of checks.
  run(&r,2*WATCH_SETTLE_MS+40);
  assert(r.phase==WATCH_HOME && bkTuned==0 && heard==0);
  run(&r,WATCH_HOME_MS-30); assert(heard==0);
  run(&r,40); assert(heard==UINT8_MAX && bkTuned!=0); // Silenced before retuning.
  talking[2]=true; run(&r,2*WATCH_SETTLE_MS+WATCH_VERIFY_MS+60);
  assert(heard==2);
  talking[2]=false; run(&r,WATCH_SAMPLE_MS+WATCH_HANG_MS+10);
  assert(r.phase==WATCH_HOME && heard==0); // Back home after the talk.

  // Suspending BK silences a BK background too; resuming goes home again.
  WATCH_SuspendBk(&r,true,now,&ops); assert(heard==UINT8_MAX);
  WATCH_SuspendBk(&r,false,now,&ops);
  run(&r,2*WATCH_SETTLE_MS+60); assert(r.phase==WATCH_HOME && heard==0);

  // Failures block a target for a while; all blocked retries later.
  reset();
  assert(WATCH_Start(&r,pair,2,UINT8_MAX,now));
  signalOK=false; tick(&r,0); run(&r,WATCH_SETTLE_MS+10);
  assert(r.blocked==1);
  run(&r,WATCH_SETTLE_MS+10); assert(r.blocked==3 && r.phase==WATCH_RETRY);
  signalOK=true; talking[0]=true;
  run(&r,WATCH_RETRY_MS+WATCH_SETTLE_MS+WATCH_VERIFY_MS+40);
  assert(heard==0);
  // A speaker failure is not a talk: the event is skipped.
  reset(); selectOK=false;
  assert(WATCH_Start(&r,pair,2,UINT8_MAX,now));
  talking[0]=true; tick(&r,0); run(&r,WATCH_SETTLE_MS+WATCH_VERIFY_MS+10);
  assert(WATCH_Owner(&r)==UINT8_MAX && (r.blocked&1));
  // Clock wrap keeps every deadline working.
  reset(); now=UINT32_MAX-100;
  assert(WATCH_Start(&r,pair,2,UINT8_MAX,now));
  talking[1]=true; run(&r,2*WATCH_SETTLE_MS+WATCH_VERIFY_MS+40);
  assert(heard==1);
  WATCH_Stop(&r); assert(r.phase==WATCH_OFF && WATCH_Owner(&r)==UINT8_MAX);
  return 0;
}
