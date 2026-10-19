#ifndef RECEIVER_H
#define RECEIVER_H
#include "core/radio-model.h"
#include <stdbool.h>
#include <stdint.h>

// Squelch decisions are sampled every 50 ms. Samples that only refresh the
// signal meter are slower: each bus burst to a receiver chip can be faintly
// audible (BK1080 on the UV-K1, SI4732 RSQ on modified radios).
#define SQL_DELAY 50
// SI4732 meter period; 0 stops its RSQ reads for a hardware-noise A/B.
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
// How often a meter-only sample of this plan may touch its chip, in ms;
// 0 never. The BK1080 is read once a second whatever its squelch.
static inline uint32_t RECEIVER_MeterPeriodMs(const RxPlan *plan) {
  return plan->device == RX_DEVICE_BROADCAST ? 1000u
       : plan->device == RX_DEVICE_SI ? UVK6_FM_RSQ_POLL_MS : 200u;
}
// Main-loop receiver upkeep. SQ0 without a receive tone has nothing to
// decide: it stays open and samples only for the meter. Otherwise squelch
// is sampled every SQL_DELAY (BK1080: once a second).
void RECEIVER_Poll(uint32_t now);
#endif
