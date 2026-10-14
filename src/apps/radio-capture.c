// FC: BK4819 frequency capture with a temporary NFM preview.
#include "apps/radio-internal.h"

CaptureRuntime capture;
CapturePhase capturePhase;
static uint32_t capturePollAt;

// Runtime keeps the committed origin throughout detection and temporary preview.
static bool capturePlan(uint32_t hz, RxPlan *plan) {
  return RADIO_MODEL_Plan(hz, (RadioParameters){RX_NFM,12500,12000,4},
      RECEIVER_Capabilities(), plan)==RX_OK &&
      plan->device==RX_DEVICE_BK && plan->frequency_hz==hz;
}

void cancelCapture(const char *status) {
  RX_DEVICE_CaptureStop();
  capturePhase=FC_OFF;
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
  capturePhase=FC_DETECT;
  capturePollAt=Now();
  message=NULL;
  gRedrawScreen=true;
}

void startCapture(void) {
  if (activeSlot.source!=RADIO_SOURCE_VFO) { notify("FC: VFO only"); return; }
  if (!runtime.active || !pauseWatch()) return;
  CAPTURE_Start(&capture, CAPTURE_AUTO, Now());
  captureAttempt();
}

void captureUpdate(void) {
  if (capturePhase!=FC_DETECT || Now()-capturePollAt<20u) return;
  capturePollAt=Now();
  uint32_t hz=0;
  CaptureSample sample=RX_DEVICE_CapturePoll(capture.range, &hz);
  RxPlan plan;
  if (sample==CAPTURE_SAMPLE && !capturePlan(hz, &plan)) sample=CAPTURE_INVALID;
  CaptureAction action=CAPTURE_Update(&capture, Now(), sample, hz);
  if (action==CAPTURE_TIMEOUT) cancelCapture("FC timeout");
  else if (action==CAPTURE_RESTART) captureAttempt();
  else if (action==CAPTURE_FOUND) {
    RX_DEVICE_CaptureStop();
    if (!applyReceiver(NULL, &plan)) { cancelCapture("Tune failed"); return; }
    capturePhase=FC_PREVIEW;
    receivingSlot=selectedSlot;
    gRedrawScreen=true;
  }
}

static void acceptCapture(void) {
  RxPlan plan;
  RX_DEVICE_CaptureStop();
  capturePhase=FC_OFF;
  if (!capturePlan(capture.last_hz, &plan) || !apply(&plan)) {
    cancelCapture("Tune failed"); return;
  }
  // apply has queued a deferred save; include these changes in that one save.
  activeSlot.tx.enabled=false;
  activeSlot.channel_number=0;
  gDeviceConfig.radio.slots[selectedSlot]=activeSlot;
  CHANNEL_MANAGER_LoadName(selectedSlot, 0);
  delayWatch();
}

// While FC or numeric editing owns the keyboard, keep all other actions
// blocked; especially never allow an input digit to become a global shortcut.
void captureKey(KEY_Code_t key, Key_State_t state) {
  if (key==KEY_PTT && state==KEY_RELEASED) RADIO_APP_PttRelease();
  if (state != KEY_RELEASED) return;
  if (key==KEY_EXIT || (key==KEY_MENU && capturePhase==FC_DETECT))
    cancelCapture(NULL);
  else if (key==KEY_MENU) acceptCapture();
  else if (key==KEY_SIDE1 || key==KEY_SIDE2) {
    RX_DEVICE_CaptureStop();
    CAPTURE_Start(&capture, (capture.scope+1u)%3u, Now());
    captureAttempt();
  }
}
