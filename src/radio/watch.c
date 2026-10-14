// Watch over every enabled slot; the primary is restored whenever watch pauses.
#include "radio/radio-internal.h"

WatchRuntime watcher;
static uint32_t watchResumeAt;
static bool watchDelay;
const char *watchStatus;
void __attribute__((noinline)) delayWatch(void) {
  watchDelay = true;
  watchResumeAt = Now() + 2000u;
}

bool RADIO_IsWatching(void) { return watcher.phase!=WATCH_OFF; }

static bool restorePrimary(void) {
  RxPlan plan;
  if (TUNING_ExactPlan(activeSlot.current.frequency_hz,activeSlot.current.parameters,
      RECEIVER_Capabilities(),&plan)!=RX_OK) {
    RX_RuntimeStop(&runtime,stopReceiver,NULL); notify("RX unavailable"); return false;
  }
  if (runtime.active && receivingSlot==selectedSlot && WATCH_SameReceiver(&runtime.current,&plan))
    return true;
  if (RX_RuntimeTune(&runtime,&plan,applyReceiver,stopReceiver,NULL)!=RX_APPLIED) {
    // A restored background watch plan is never a PTT/primary substitute.
    RX_RuntimeStop(&runtime,stopReceiver,NULL); notify("RX error"); return false;
  }
  receivingSlot=selectedSlot; gRedrawScreen=true;
  return true;
}
bool pauseWatch(void) { return pauseWatchTo(NULL); }
// Stops watching and leaves target (or, by default, the selected slot) tuned.
bool pauseWatchTo(const WatchTarget *target) {
  bool wasWatching=watcher.phase!=WATCH_OFF;
  if (wasWatching) {
    // Watch may have left any chip tuned elsewhere: stop them all and let
    // restorePrimary configure the selected slot from scratch.
    RECEIVER_Mute();
    for (uint8_t device=0; device<8; ++device)
      if (watcher.devices&(1u<<device)) RX_DEVICE_StopChip((RxDevice)device);
    runtime.active=false;receivingSlot=UINT8_MAX;
  }
  WATCH_Stop(&watcher);
  // Manual RF changes push the restart back; with no watch pending (start-up,
  // single slot) there is nothing to delay.
  if (wasWatching || watchDelay) delayWatch();
  if (!wasWatching) return true;
  if (!target) return restorePrimary();
  if (RX_RuntimeTune(&runtime,&target->plan,applyReceiver,stopReceiver,NULL)!=RX_APPLIED) {
    RX_RuntimeStop(&runtime,stopReceiver,NULL); notify("RX error"); return false;
  }
  receivingSlot=target->id; gRedrawScreen=true;
  return true;
}
static bool watchSignal(void *unused, const RxPlan *plan, bool *found) {
  (void)unused; return RX_DEVICE_WatchSignal(plan,found);
}
static bool watchPrepare(void *unused, const WatchTarget *target) {
  (void)unused;
  // A broadcast chip already playing this plan needs no retune; BK is
  // retuned by polling, so it is always configured again.
  if (target->plan.device!=RX_DEVICE_BK && runtime.active &&
      WATCH_SameReceiver(&runtime.current,&target->plan)) return true;
  if (!RX_DEVICE_ApplyConcurrent(&target->plan)) return false;
  if (target->plan.device==RX_DEVICE_SI) resetRds();
  return true;
}
static bool watchSelect(void *unused, const WatchTarget *target, bool open) {
  (void)unused;
  if (!RECEIVER_Select(&target->plan,watcher.devices,open)) {
    gReceiver.open=false;runtime.active=false;receivingSlot=UINT8_MAX;
    gRedrawScreen=true;return false;
  }
  runtime.current=target->plan;runtime.active=true;receivingSlot=target->id;
  gRedrawScreen=true;return true;
}
static uint32_t watchNow(void *unused) { (void)unused; return Now(); }
static const WatchOperations watchOperations={
  .prepare=watchPrepare,.signal=watchSignal,.select=watchSelect,.now=watchNow
};
void suspendWatchBk(bool suspend) {
  WATCH_SuspendBk(&watcher,suspend,Now(),&watchOperations);
}
void watchUpdate(void) {
  // Menus, input and prompts leave watch running; only features that own
  // the RF chips (each pauses watch on entry) hold it here.
  if (capturing || seek.active || spectrumOn || RADIO_TxBusy()) return;
  if (gDeviceConfig.radio.slot_count < 2) {
    watchStatus=NULL;
    if (watcher.phase!=WATCH_OFF) (void)pauseWatch();
    return;
  }
  if (watchDelay) {
    if ((int32_t)(Now()-watchResumeAt)<0) return;
    watchDelay=false;
  }
  if (watcher.phase==WATCH_OFF) {
    if (!runtime.active) return;
    WatchTarget targets[RADIO_WATCH_LIMIT];
    uint8_t count=0;
    watchStatus=NULL;
    for (uint8_t id=0; id<gDeviceConfig.radio.slot_count; ++id) {
      RadioSlot effective;
      if (!effectiveSlot(id, &effective, NULL)) {
        watchStatus="DW: unavailable"; continue;
      }
      RxPlan plan;
      RxResult result=TUNING_ExactPlan(effective.current.frequency_hz,effective.current.parameters,
          RECEIVER_Capabilities(),&plan);
      if (result!=RX_OK) {
        watchStatus="DW: unavailable"; continue;
      }
      if (!WATCH_Eligible(&plan)) { watchStatus="DW: SSB off"; continue; }
      targets[count++]=(WatchTarget){id,plan};
    }
    if (!WATCH_Start(&watcher,targets,count,selectedSlot,Now())) {
      // Duplicates and extra broadcasts collapse: one source is not a watch.
      // Expected (e.g. two FM broadcasts), so the user manual explains it.
      watchDelay=true; watchResumeAt=Now()+WATCH_RETRY_MS; return;
    }
    RECEIVER_Mute(); // Watch owns the speaker from here.
    receivingSlot=UINT8_MAX;
  }
  watchStatus=NULL;
  bool wasRetrying=watcher.phase==WATCH_RETRY;
  WATCH_Update(&watcher,Now(),&watchOperations);
  // Refresh RSSI for display; Watch, not squelch, owns the audio gate here.
  if (WATCH_Owner(&watcher)!=UINT8_MAX) (void)RECEIVER_Measure();
  // If all candidates fail, listen to the primary during bounded retry delay.
  if (watcher.phase==WATCH_RETRY && !wasRetrying) (void)restorePrimary();
}
