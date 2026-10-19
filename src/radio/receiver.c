#include "radio/receiver.h"
#include "app/settings.h"
#include "drivers/bk1080/bk1080.h"
#include "drivers/bk4819/bk4819.h"
#include "drivers/si473x/si473x.h"
#include "devices/backlight.h"
#include "devices/led.h"
#include "devices/rx-device.h"
#include "devices/tx-device.h"
#include "devices/display.h"

Receiver gReceiver;

ReceiverCapabilities RECEIVER_Capabilities(void) {
  static bool detected;
  static ReceiverCapabilities capabilities;
  if (!detected) {
    capabilities.broadcast = BK1080_Detect();
    // Probe each chip through its own driver. A failed BK read is not SI.
    capabilities.si = !capabilities.broadcast && SI47XX_Detect();
    capabilities.ssb_patch = capabilities.si && SETTINGS_IsPatchPresent();
    detected = true;
  }
  return capabilities;
}

void RECEIVER_Init(void) {
  static bool initialized;
  if (initialized) return;
  initialized = true;
  gReceiver = (Receiver){.plan = {RX_DEVICE_BK, 145500000u, {RX_NFM, 12500u, 12500u, 0, 0}}};
  BK4819_Init();
  TX_DEVICE_Disable(); // Explicitly keep PA off on startup.
  BK4819_RX_TurnOn();
  (void)BK1080_Init(0, false);
}

void RECEIVER_SetOpen(bool open) {
  gReceiver.open = open;
  RX_DEVICE_SetAudio(gReceiver.plan.device, open);
  LED_SetGreen(open);
  BACKLIGHT_SetReception(open);
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
  if (!RX_DEVICE_Apply(plan, gReceiver.plan.device))
    return false;
  commit(plan);
  return true;
}

bool RECEIVER_Select(const RxPlan *plan, uint8_t devices, bool open) {
  if (!plan || !RX_DEVICE_SelectAudio(devices, plan->device, open))
    return false;
  commit(plan);
  gReceiver.open = open;
  LED_SetGreen(open);
  BACKLIGHT_SetReception(open);
  return true;
}

bool RECEIVER_Measure(void) {
  bool open;
  (void)RX_DEVICE_Sample(&gReceiver.plan, &gReceiver.rssi, &open);
  return open;
}

void RECEIVER_UpdateSquelch(void) {
  bool open = RECEIVER_Measure();
  if (open != gReceiver.open) {
    RECEIVER_SetOpen(open);
    gRedrawScreen = true;
  }
}
