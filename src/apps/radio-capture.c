// FC: BK4819 frequency capture. A confirmed carrier is tuned like typed input.
#include "apps/radio-internal.h"

CaptureRuntime capture;
#if UVK6_ENABLE_FC
bool capturing;
static uint32_t capturePollAt;

// Runtime keeps the committed origin until a carrier is confirmed.
static bool capturePlan(uint32_t hz, RxPlan *plan) {
  return RADIO_MODEL_Plan(hz, (RadioParameters){RX_NFM,12500,12000,4},
      RECEIVER_Capabilities(), plan)==RX_OK &&
      plan->device==RX_DEVICE_BK && plan->frequency_hz==hz;
}

void cancelCapture(const char *status) {
  RX_DEVICE_CaptureStop();
  capturing=false;
  if (!applyReceiver(NULL, &runtime.current)) {
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    status="RX error";
  } else {
    runtime.active=true;
    receivingSlot=selectedSlot;
  }
  delayWatch();
  message=NULL;
  if (status) notify(status);
  gRedrawScreen=true;
}

static void captureAttempt(void) {
  RECEIVER_Mute();
  receivingSlot=UINT8_MAX;
  resetRds();
  RX_DEVICE_CaptureStart(capture.range, gReceiver.plan.device);
  capturing=true;
  capturePollAt=Now();
  message=NULL;
  gRedrawScreen=true;
}

void startCapture(void) {
  if (activeSlot.source!=RADIO_SOURCE_VFO) { notify("VFO only"); return; }
  if (!runtime.active || !pauseWatch()) return;
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
  CHANNEL_MANAGER_LoadName(selectedSlot, 0);
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

// While FC owns the keyboard every other action stays blocked; especially
// never allow a digit to become a global shortcut.
void captureKey(KEY_Code_t key, Key_State_t state) {
  if (key==KEY_PTT && state==KEY_RELEASED) RADIO_APP_PttRelease();
  if (state != KEY_RELEASED) return;
  if (key==KEY_EXIT || key==KEY_MENU) cancelCapture(NULL);
  else if (key==KEY_SIDE1 || key==KEY_SIDE2) {
    RX_DEVICE_CaptureStop();
    CAPTURE_Start(&capture, (capture.scope+1u)%3u, Now());
    captureAttempt();
  }
}
#else
void startCapture(void) { notify("Not supported"); }
void captureUpdate(void) {}
void captureKey(KEY_Code_t key, Key_State_t state) { (void)key; (void)state; }
void cancelCapture(const char *status) { (void)status; }
#endif
