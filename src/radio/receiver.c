#include "radio/receiver.h"
#include "devices/backlight.h"
#include "devices/led.h"
#include "devices/rx-device.h"
#include "devices/tx-device.h"
#include "devices/display.h"

Receiver gReceiver;

ReceiverCapabilities RECEIVER_Capabilities(void) { return RX_DEVICE_Capabilities(); }

void RECEIVER_Init(void) {
  static bool initialized;
  if (initialized) return;
  initialized = true;
  gReceiver = (Receiver){.plan = {RX_DEVICE_BK, 145500000u, {RX_NFM, 12500u, 12500u, 0, 0}}};
  RX_DEVICE_Init();
}

void RECEIVER_SetOpen(bool open) {
  bool ok = RX_DEVICE_RouteAudio(gReceiver.plan.device, open);
  gReceiver.open = open && ok;
  LED_SetGreen(gReceiver.open);
  BACKLIGHT_SetReception(gReceiver.open);
}

void RECEIVER_Mute(void) {
  // Leave the chip configured for recovery, but never play stale audio.
  RECEIVER_SetOpen(false);
}

static void commit(const RxPlan *plan) {
  gReceiver.plan = *plan;
  gReceiver.rssi = 0; // Samples belong to one tuned plan.
  gRedrawScreen = true;
}

bool RECEIVER_Apply(const RxPlan *plan) {
  RxPlan resolved;
  if (!plan ||
      RADIO_MODEL_Plan(plan->frequency_hz, plan->parameters,
                       RECEIVER_Capabilities(), &resolved) != RX_OK ||
      resolved.device != plan->device ||
      resolved.frequency_hz != plan->frequency_hz)
    return false;
  RECEIVER_Mute();
  if (!RX_DEVICE_Activate(plan))
    return false;
  commit(plan);
  return true;
}

bool RECEIVER_Select(const RxPlan *plan, bool open) {
  if (!plan || !RX_DEVICE_RouteAudio(plan->device, open))
    return false;
  commit(plan);
  gReceiver.open = open;
  LED_SetGreen(gReceiver.open);
  BACKLIGHT_SetReception(gReceiver.open);
  return true;
}

bool RECEIVER_Measure(void) {
  // A failed I2C / serial sample is not a closed squelch. Preserve the
  // last valid meter value and audio gate until a valid sample arrives.
  uint16_t rssi;
  bool open = gReceiver.open;
  if (!RX_DEVICE_Sample(&gReceiver.plan, &rssi, &open))
    return gReceiver.open;
  gReceiver.rssi = rssi;
  return open;
}

void RECEIVER_UpdateSquelch(void) {
  bool open = RECEIVER_Measure();
  if (open != gReceiver.open) {
    RECEIVER_SetOpen(open);
    gRedrawScreen = true;
  }
}
