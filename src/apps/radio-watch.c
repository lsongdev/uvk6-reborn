// Dual watch over two slots; the primary is restored whenever watch pauses.
#include "apps/radio-internal.h"

WatchRuntime watcher;
static uint32_t watchResumeAt;
static bool watchDelay;
const char *watchStatus;
void __attribute__((noinline)) delayWatch(void) {
  watchDelay = true;
  watchResumeAt = Now() + 2000u;
}

bool RADIO_APP_isWatching(void) { return watcher.phase!=WATCH_OFF; }

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
bool pauseWatch(void) {
  bool wasWatching=watcher.phase!=WATCH_OFF;
  if (watcher.parallel) {
    RECEIVER_Mute();
    for (uint8_t i=0; i<watcher.count; ++i)
      RX_DEVICE_StopChip(watcher.targets[i].plan.device);
    runtime.active=false;receivingSlot=UINT8_MAX;
  }
  WATCH_Stop(&watcher);
  delayWatch();
  return !wasWatching || restorePrimary();
}
static bool watchTune(void *unused, const WatchTarget *target) {
  (void)unused;
  RxTuneResult result=RX_RuntimeTune(&runtime,&target->plan,applyReceiver,stopReceiver,NULL);
  if (result!=RX_APPLIED) return false;
  receivingSlot=target->id; gRedrawScreen=true;
  return true;
}
static bool watchSignal(void *unused, const RxPlan *plan, bool *found) {
  (void)unused; return RX_DEVICE_WatchSignal(plan,found);
}
static void watchAudio(void *unused, bool open) {
  (void)unused;
  if (gReceiver.open==open) return;
  RECEIVER_SetOpen(open); gRedrawScreen=true;
}
static bool watchPrepare(void *unused, const WatchTarget *target) {
  (void)unused;
  if (!watcher.blocked && runtime.active && WATCH_SameReceiver(&runtime.current,&target->plan)) return true;
  if (!RX_DEVICE_ApplyConcurrent(&target->plan)) return false;
  if (target->plan.device==RX_DEVICE_SI) resetRds();
  return true;
}
static bool watchSelect(void *unused, const WatchTarget *target, bool open) {
  (void)unused;
  uint8_t devices=0;
  for (uint8_t i=0; i<watcher.count; ++i) devices |= 1u<<watcher.targets[i].plan.device;
  if (!RECEIVER_Select(&target->plan,devices,open)) {
    gReceiver.open=false;runtime.active=false;receivingSlot=UINT8_MAX;
    gRedrawScreen=true;return false;
  }
  runtime.current=target->plan;runtime.active=true;receivingSlot=target->id;
  gRedrawScreen=true;return true;
}
static uint32_t watchNow(void *unused) { (void)unused; return Now(); }
static const WatchOperations watchOperations={
  .tune=watchTune,.signal=watchSignal,.audio=watchAudio,
  .prepare=watchPrepare,.select=watchSelect,.now=watchNow
};
void watchUpdate(void) {
  if (capturePhase!=FC_OFF || number.active || CHANNEL_MANAGER_Active() || inMenu || confirming || seek.active || functionPending ||
      txBusy()) return;
  if (!gDeviceConfig.radio.watch_enabled || gDeviceConfig.radio.slot_count < 2) {
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
    watchStatus="DW: select 2";
    for (uint8_t id=0; id<gDeviceConfig.radio.slot_count && count<RADIO_WATCH_LIMIT; ++id) {
      const RadioSlot *stored=&gDeviceConfig.radio.slots[id];
      RadioSlot effective;
      if (!stored->watch) continue;
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
    if (!WATCH_Start(&watcher,targets,count,selectedSlot)) {
      if (count==2) watchStatus="DW: same channel";
      watchDelay=true; watchResumeAt=Now()+WATCH_RETRY_MS; return;
    }
  }
  watchStatus=NULL;
  bool wasRetrying=watcher.phase==WATCH_RETRY;
  WATCH_Update(&watcher,Now(),&watchOperations);
  // Refresh RSSI for display; Watch, not squelch, owns the audio gate here.
  if (watcher.phase==WATCH_HOLD && watcher.signal) (void)RECEIVER_Measure();
  // If all candidates fail, listen to the primary during bounded retry delay.
  if (watcher.phase==WATCH_RETRY && !wasRetrying) (void)restorePrimary();
}
