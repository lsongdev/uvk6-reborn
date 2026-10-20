// FC: BK4819 frequency capture. A confirmed carrier is tuned like typed input.
#include "radio/radio-internal.h"

CaptureRuntime capture;
bool capturing;
static uint32_t capturePollAt;
static bool captureInterruptedAudio;

// Runtime keeps the committed origin until a carrier is confirmed.
static bool capturePlan(uint32_t hz, RxPlan *plan) {
  return RADIO_MODEL_Plan(hz, (RadioParameters){RX_NFM,12500,12000,4},
      RECEIVER_Capabilities(), plan)==RX_OK &&
      plan->device==RX_DEVICE_BK && plan->frequency_hz==hz;
}

void cancelCapture(const char *status) {
  RX_DEVICE_CaptureStop();
  capturing=false;
  if (watcher.phase!=WATCH_OFF) { // BK slots rejoin the watch.
    suspendWatchBk(false);
  } else if (!captureInterruptedAudio) { // An independent broadcast never stopped.
    runtime.active=true;
    receivingSlot=selectedSlot;
  } else if (!applyReceiver(NULL, &runtime.current)) {
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    status="RX error";
  } else {
    runtime.active=true;
    receivingSlot=selectedSlot;
  }
  if (watcher.phase==WATCH_OFF) delayWatch();
  message=NULL;
  if (status) notify(status);
  gRedrawScreen=true;
}

// FC borrows BK alone: a receiver on BK goes quiet, a broadcast chip plays on.
static void captureAttempt(void) {
  captureInterruptedAudio = !RX_DEVICE_CanRunTogether(RX_DEVICE_BK,gReceiver.plan.device);
  if (captureInterruptedAudio) {
    RECEIVER_Mute();
    receivingSlot=UINT8_MAX;
    resetRds();
  }
  if (!RX_DEVICE_CaptureStart(capture.range)) {
    cancelCapture("RX error");
    return;
  }
  capturing=true;
  capturePollAt=Now();
  message=NULL;
  gRedrawScreen=true;
}

void RADIO_StartCapture(void) {
  if (toneScanning) return;
  if (activeSlot.source!=RADIO_SOURCE_VFO) { notify("VFO only"); return; }
  // FC borrows BK: BK slots leave the watch until FC ends, broadcasts go on.
  if (watcher.phase!=WATCH_OFF) suspendWatchBk(true);
  else if (!runtime.active) return;
  CAPTURE_Start(&capture, CAPTURE_AUTO, Now());
  captureAttempt();
}

// The carrier replaces the VFO frequency in NFM with TX off and no channel
// binding; apply queues the one save, which includes these changes.
static void adoptCapture(const RxPlan *plan) {
  RX_DEVICE_CaptureStop();
  capturing=false;
  if (!apply(plan)) { cancelCapture("Tune failed"); return; }
  activeSlot.tx.enabled=false;
  activeSlot.channel_number=0;
  gDeviceConfig.radio.slots[selectedSlot]=activeSlot;
  delayWatch();
}

void captureUpdate(void) {
  if (!capturing || Now()-capturePollAt<20u) return;
  capturePollAt=Now();
  uint32_t hz=0;
  CaptureSample sample=RX_DEVICE_CapturePoll(capture.range, &hz);
  RxPlan plan;
  if (sample==CAPTURE_SAMPLE && !capturePlan(hz, &plan)) sample=CAPTURE_INVALID;
  CaptureAction action=CAPTURE_Update(&capture, Now(), sample, hz);
  if (action==CAPTURE_RESTART) captureAttempt();
  else if (action==CAPTURE_FOUND) adoptCapture(&plan);
}

// MENU on the FC screen: adopt the reading shown (nothing yet: ignored).
void RADIO_CaptureAdopt(void) {
  RxPlan plan;
  if (capturing && capture.last_hz && capturePlan(capture.last_hz, &plan)) adoptCapture(&plan);
}
void RADIO_CaptureCancel(void) { if (capturing) cancelCapture(NULL); }
void RADIO_CaptureNextScope(void) {
  if (!capturing) return;
  RX_DEVICE_CaptureStop();
  CAPTURE_Start(&capture, (capture.scope+1u)%3u, Now());
  captureAttempt();
}
bool RADIO_Capturing(void) { return capturing; }
