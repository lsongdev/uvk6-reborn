#ifndef RECEIVER_H
#define RECEIVER_H
#include "core/radio-model.h"
#include <stdbool.h>
#include <stdint.h>

// How often the main loop samples signal and squelch, in ms. The BK1080
// is read only once a second: on the UV-K1 every I2C burst to it is
// faintly audible in the broadcast audio, and the reference reads it never
// during playback. Its SNR squelch and meter need no faster updates.
static inline uint32_t RECEIVER_SamplePeriodMs(RxDevice device) {
  return device == RX_DEVICE_BROADCAST ? 1000u : 50u;
}

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
