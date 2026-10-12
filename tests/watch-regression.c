#include "core/watch.h"
#include <assert.h>

static uint32_t now;
static unsigned tunes, samples, audioChanges;
static bool found, signalOK=true, tuneOK=true, audible;
static uint8_t tunedId;
static uint32_t tuneLatency;
static bool tune(void *unused, const WatchTarget *target) {
  (void)unused; ++tunes; tunedId=target->id; now+=tuneLatency; return tuneOK;
}
static bool signal(void *unused, const RxPlan *plan, bool *out) {
  (void)unused; (void)plan; ++samples; *out=found; return signalOK;
}
static void audio(void *unused, bool open) {
  (void)unused; audible=open; ++audioChanges;
}
static uint32_t clockNow(void *unused) { (void)unused; return now; }
static const WatchOperations ops={.tune=tune,.signal=signal,.audio=audio,.now=clockNow};
static WatchTarget pair[]={
  {0,{RX_DEVICE_BK,145500000,{RX_NFM,12500,12000,4,0}}},
  {1,{RX_DEVICE_SI,100000000,{RX_WFM,100000,0,0,0}}}
};
static void tick(WatchRuntime *r, uint32_t elapsed) { now+=elapsed; WATCH_Update(r,now,&ops); }
int main(void) {
  WatchRuntime r={0};
  assert(!WATCH_Start(&r,pair,0) && !WATCH_Start(&r,pair,1));
  assert(!WATCH_Start(&r,pair,RADIO_WATCH_LIMIT+1));
  WatchTarget duplicate[]={pair[0],pair[0]};duplicate[1].id=1;
  assert(!WATCH_Start(&r,duplicate,2));
  duplicate[1].plan.parameters.step_hz=25000;
  assert(!WATCH_Start(&r,duplicate,2)); // Step is not a distinct RF receive path.
  duplicate[1].plan.parameters.rx_tone=1;
  assert(WATCH_Start(&r,duplicate,2)); // Different tone is a real discriminator.
  duplicate[1].plan.parameters.mode=RX_USB;
  assert(!WATCH_Start(&r,duplicate,2));
  assert(WATCH_Start(&r,pair,2));
  tuneLatency=200; tick(&r,0);
  assert(tunes==1 && tunedId==0 && r.phase==WATCH_SETTLE && !audible);
  tick(&r,179);assert(!samples); // Wait starts after the blocking tune.
  found=true;tick(&r,1);assert(r.phase==WATCH_VERIFY && !audible);
  tick(&r,WATCH_VERIFY_MS-1);assert(samples==1);
  tick(&r,1);assert(r.phase==WATCH_HOLD && audible && !WATCH_IsProbing(&r));
  found=false;tick(&r,WATCH_SAMPLE_MS);assert(r.phase==WATCH_HANG && !audible);
  found=true;tick(&r,WATCH_SAMPLE_MS);assert(r.phase==WATCH_HOLD && audible);
  // A continuous broadcaster cannot starve the other frequency indefinitely.
  tick(&r,WATCH_MAX_HOLD_MS);assert(r.phase==WATCH_TUNE && !audible);
  tick(&r,0);assert(tunedId==1 && tunes==2);
  unsigned before=samples;tick(&r,349);assert(samples==before);
  tick(&r,1);assert(r.phase==WATCH_VERIFY);
  found=false;tick(&r,WATCH_VERIFY_MS);assert(r.phase==WATCH_TUNE && !audible);
  tuneLatency=0;tick(&r,0);found=true;tick(&r,180);tick(&r,80);
  found=false;tick(&r,50);tick(&r,WATCH_HANG_MS-1);assert(r.phase==WATCH_HANG);
  tick(&r,50);assert(r.phase==WATCH_TUNE);
  // Failure retries are bounded, and no sample happens after a failed apply.
  WATCH_Stop(&r);assert(WATCH_Start(&r,pair,2));tuneOK=false;
  unsigned beforeSamples=samples;tick(&r,0);tick(&r,0);tick(&r,0);
  assert(r.phase==WATCH_RETRY && samples==beforeSamples);
  unsigned beforeTunes=tunes;tick(&r,WATCH_RETRY_MS-1);assert(tunes==beforeTunes);
  tuneOK=true;tick(&r,1);assert(tunes==beforeTunes+1);
  signalOK=false;tick(&r,r.targets[r.cursor].plan.device==RX_DEVICE_BK ? 180 : 350);
  assert(r.phase==WATCH_TUNE && !audible);
  // Timer rollover: stable/verify deadlines and hold duration remain valid.
  WATCH_Stop(&r);signalOK=true;now=UINT32_MAX-100;found=true;
  assert(WATCH_Start(&r,pair,2));tick(&r,0);tick(&r,179);assert(r.phase==WATCH_SETTLE);
  tick(&r,1);assert(r.phase==WATCH_VERIFY);tick(&r,80);assert(r.phase==WATCH_HOLD);
  tick(&r,WATCH_MAX_HOLD_MS);assert(r.phase==WATCH_TUNE);
  WATCH_Stop(&r);beforeTunes=tunes;tick(&r,10000);assert(tunes==beforeTunes);
  assert(!WATCH_IsProbing(&r) && r.phase==WATCH_OFF && audioChanges>0);
  return 0;
}
