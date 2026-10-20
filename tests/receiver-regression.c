// Receiver commit/restore rules with the hardware boundary mocked.
#include "../src/radio/receiver.c"
#include <assert.h>
#include <string.h>
bool gRedrawScreen;

static bool hasSi = true, hardwareApplyOk = true, routeOK = true;
static unsigned probes, hardwareApplies, mutes, opens;
static RxDevice lastPrevious;
bool RX_DEVICE_Activate(const RxPlan *plan) {
  (void)plan; lastPrevious = gReceiver.plan.device; ++hardwareApplies;
  return hardwareApplyOk;
}
bool RX_DEVICE_RouteAudio(RxDevice device, bool open) {
  (void)device; if (open) ++opens; else ++mutes; return routeOK;
}
static uint16_t sampleRssi = 42;
static bool sampleOpen, sampleOK = true;
static unsigned samples;
bool RX_DEVICE_Sample(const RxPlan *plan, uint16_t *rssi, bool *open) {
  (void)plan; ++samples; *rssi = sampleRssi; *open = sampleOpen; return sampleOK;
}
void LED_SetGreen(bool on) { (void)on; }
void BACKLIGHT_SetReception(bool open) { (void)open; }
ReceiverCapabilities RX_DEVICE_Capabilities(void) {
  ++probes; return (ReceiverCapabilities){.si = hasSi, .broadcast = !hasSi, .ssb_patch = hasSi, .bk_am=true, .bk_bandwidth_mask=RX_BK_BANDWIDTH_ALL};
}
static unsigned chipInitializations;
void RX_DEVICE_Init(void) { ++chipInitializations; }

void testFrequencyInput(void);

// Samples since the previous call.
static unsigned sampled(void) { unsigned n = samples; samples = 0; return n; }
// Polls every millisecond across [from, to).
static uint32_t pollFor(uint32_t from, uint32_t to) {
  for (uint32_t t = from; t < to; ++t) RECEIVER_Poll(t);
  return to;
}
static void testPoll(void) {
  // SQ0 without a tone opens without waiting on a sample; the meter of a
  // BK plan is read every 200 ms, not at the 50 ms squelch rate.
  gReceiver.plan = (RxPlan){RX_DEVICE_BK, 145500000u, {RX_NFM, 12500, 12000, 0, 0}};
  gReceiver.open = false; sampleOpen = false; sampled();
  uint32_t t = pollFor(10000, 11000);
  assert(gReceiver.open && sampled() == 5);
  // SQ0 with a receive tone still decides: 50 ms squelch samples.
  gReceiver.plan.parameters.rx_tone = 1;
  t = pollFor(t, t + 1000);
  assert(!gReceiver.open && sampled() == 20);
  // A squelched plan: 50 ms.
  gReceiver.plan.parameters.rx_tone = 0; gReceiver.plan.parameters.squelch = 4;
  t = pollFor(t, t + 1000); assert(sampled() == 20);
  // BK1080: once a second, squelched or not, and the meter still updates.
  gReceiver.plan = (RxPlan){RX_DEVICE_BROADCAST, 95900000u, {RX_WFM, 100000, 0, 4, 0}};
  t = pollFor(t, t + 3000); assert(sampled() == 3);
  gReceiver.plan.parameters.squelch = 0; sampleRssi = 77;
  t = pollFor(t, t + 3000); assert(sampled() == 3 && gReceiver.rssi == 77 && gReceiver.open);
  // SI4732 meter: 500 ms.
  gReceiver.plan = (RxPlan){RX_DEVICE_SI, 95900000u, {RX_WFM, 100000, 0, 0, 0}};
  t = pollFor(t, t + 2000); assert(sampled() == 4);
  // A failed route at SQ0 is retried at the squelch cadence, not every loop.
  gReceiver.open = false; routeOK = false; opens = 0;
  (void)pollFor(t, t + 1000);
  assert(!gReceiver.open && opens == 20);
  routeOK = true; sampleRssi = 42;
}

int main(int argc, char **argv) {
  (void)argv;
  hasSi = argc == 1;
  RECEIVER_Init();
  RECEIVER_Init();
  assert(chipInitializations == 1); // Re-entering the UI must not reset RF.
  assert(gReceiver.plan.device == RX_DEVICE_BK && gReceiver.plan.frequency_hz == 145500000u);
  RxPlan plan;
  assert(RADIO_MODEL_Plan(100000000, (RadioParameters){RX_WFM, 100000, 0, 0},
         RECEIVER_Capabilities(), &plan) == RX_OK);
  // A failed hardware apply mutes but keeps the previous committed plan.
  hardwareApplyOk = false;
  assert(!RECEIVER_Apply(&plan));
  assert(hardwareApplies == 1 && mutes > 0 && lastPrevious == RX_DEVICE_BK);
  assert(gReceiver.plan.device == RX_DEVICE_BK && gReceiver.plan.frequency_hz == 145500000u);
  hardwareApplyOk = true;
  gReceiver.rssi = 99;
  assert(RECEIVER_Apply(&plan));
  assert(gReceiver.plan.frequency_hz == 100000000u && gReceiver.rssi == 0 &&
         gReceiver.plan.device == (hasSi ? RX_DEVICE_SI : RX_DEVICE_BROADCAST));
  // A plan inconsistent with the model is rejected before touching hardware.
  RxPlan invalid = plan; invalid.device = RX_DEVICE_BK;
  assert(!RECEIVER_Apply(&invalid) && hardwareApplies == 2);
  // Squelch updates sample the tuned chip and gate audio only on change.
  sampleOpen = true; RECEIVER_UpdateSquelch();
  assert(gReceiver.open && gReceiver.rssi == 42);
  unsigned before = mutes;
  // A transient RSQ NACK must not cut off an FM station or zero RSSI.
  sampleOK = false; sampleOpen = false; sampleRssi = 0;
  RECEIVER_UpdateSquelch();
  assert(gReceiver.open && gReceiver.rssi == 42 && mutes == before);
  sampleOK = true; sampleRssi = 42;
  sampleOpen = false; RECEIVER_UpdateSquelch();
  assert(!gReceiver.open && mutes == before + 1);
  RECEIVER_UpdateSquelch(); assert(mutes == before + 1);
  // Watch routing commits the selected plan without retuning.
  RxPlan bk = {RX_DEVICE_BK, 438500000u, {RX_NFM, 12500, 12000, 1, 0}};
  routeOK = false;
  assert(!RECEIVER_Select(&bk,true));
  routeOK = true;
  assert(RECEIVER_Select(&bk,true));
  assert(gReceiver.plan.frequency_hz == 438500000u && gReceiver.open && hardwareApplies == 2);
  testPoll();
  testFrequencyInput();
  return 0;
}
