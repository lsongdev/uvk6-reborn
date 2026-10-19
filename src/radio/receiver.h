#ifndef RECEIVER_H
#define RECEIVER_H
#include "core/radio-model.h"
#include <stdbool.h>
#include <stdint.h>

#define SQL_DELAY 50
// BK1080 I2C reads can be audible on the UV-K1 during broadcast playback.
// Preserve main's slower sampling cadence for ordinary squelch checks.
static inline uint32_t RECEIVER_SamplePeriodMs(RxDevice device) {
  return device == RX_DEVICE_BROADCAST ? 1000u : SQL_DELAY;
}
#define WATCH_METER_POLL_MS 200u
// SI4732 FM SQ0 audio is continuous; RSQ is only a signal meter. Zero
// disables its periodic I2C reads for a hardware-noise comparison.
#ifndef UVK6_FM_RSQ_POLL_MS
#define UVK6_FM_RSQ_POLL_MS 500u
#endif

// What the receiver hardware is doing now. Any owner (user tune, Seek,
// Watch, TX restore) changes it only through RECEIVER_Apply/Select, so the
// device, frequency and parameters always describe the chip being heard.
typedef struct {
  RxPlan plan;
  uint16_t rssi; // Last sample of plan.device, raw chip units.
  bool open;     // Squelch open and speaker routed.
} Receiver;

extern Receiver gReceiver;

ReceiverCapabilities RECEIVER_Capabilities(void);
void RECEIVER_Init(void);
// Tune one chip and make it the audible receiver. Mutes first; on failure
// the previous plan is kept so the caller may restore it.
bool RECEIVER_Apply(const RxPlan *plan);
// Watch: route audio between chips already configured concurrently.
bool RECEIVER_Select(const RxPlan *plan, bool open);
void RECEIVER_SetOpen(bool open);
void RECEIVER_Mute(void);
// Sample RSSI/squelch; returns whether squelch is open.
bool RECEIVER_Measure(void);
void RECEIVER_UpdateSquelch(void);
#endif
