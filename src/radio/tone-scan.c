#include "radio/radio-internal.h"

bool toneScanning;
static RadioTone result, candidate;
static uint8_t hits;
static uint32_t sampledAt;

const RadioTone *RADIO_ToneScanResult(void) { return toneScanning ? &result : NULL; }

bool RADIO_StopToneScan(bool adopt) {
  if (!toneScanning) return false;
  if (adopt && (!result || !requireVfoSource())) return false;
  toneScanning = false;
  RX_DEVICE_StopChip(RX_DEVICE_BK);
  bool ok;
  if (adopt) {
    RxPlan plan = runtime.current;
    plan.parameters.rx_tone = result;
    ok = apply(&plan);
  } else {
    ok = applyReceiver(NULL, &runtime.current);
    if (!ok) RX_RuntimeStop(&runtime, stopReceiver, NULL);
    else { runtime.active = true; receivingSlot = selectedSlot; }
  }
  if (!ok) notify("RX error");
  delayWatch();
  gRedrawScreen = true;
  return ok;
}

bool RADIO_StartToneScan(void) {
  if (toneScanning || capturing || seek.active || spectrumOn || RADIO_TxBusy()) return false;
  if (activeSlot.current.parameters.mode != RX_NFM) { notify("NFM only"); return false; }
  // A talkback may have left another slot tuned even with watch stopped.
  // Reuse watch's restoration without committing any user edit.
  if (!pauseWatch() || !restoreSelectedReceiver()) return false;
  RECEIVER_Mute();
  receivingSlot = UINT8_MAX;
  resetRds();
  result = candidate = hits = 0;
  sampledAt = Now();
  toneScanning = true;
  message = NULL;
  if (!RX_DEVICE_ToneScanStart(&runtime.current)) {
    RADIO_StopToneScan(false);
    notify("RX error");
    return false;
  }
  gRedrawScreen = true;
  return true;
}

void toneScanUpdate(void) {
  uint32_t now=Now();
  if (result || now-sampledAt < 200u) return;
  sampledAt = now;
  RadioTone tone;
  if (RX_DEVICE_ToneScanPoll(&tone) == TONE_SCAN_BUSY) return;
  if (!tone || tone != candidate) hits = 0;
  candidate = tone;
  if (tone && ++hits >= 3) { result = tone; gRedrawScreen = true; }
}
