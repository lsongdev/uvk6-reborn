// Receiver commit/restore rules with the hardware boundary mocked.
#include "../src/radio/receiver.c"
#include <assert.h>
#include <string.h>
bool gRedrawScreen;

static bool hasSi = true, hardwareApplyOk = true;
static unsigned probes, hardwareApplies, mutes;
static RxDevice lastPrevious;
bool RX_DEVICE_Apply(const RxPlan *plan, RxDevice previous) {
  (void)plan; lastPrevious = previous; ++hardwareApplies;
  return hardwareApplyOk;
}
void RX_DEVICE_SetAudio(RxDevice device, bool open) { (void)device; if (!open) ++mutes; }
bool RX_DEVICE_SelectAudio(uint8_t devices, RxDevice selected, bool open) {
  (void)open; return devices & (1u << selected);
}
static uint16_t sampleRssi = 42;
static bool sampleOpen;
bool RX_DEVICE_Sample(const RxPlan *plan, uint16_t *rssi, bool *open) {
  (void)plan; *rssi = sampleRssi; *open = sampleOpen; return true;
}
void LED_SetGreen(bool on) { (void)on; }
void BACKLIGHT_SetReception(bool open) { (void)open; }
ReceiverCapabilities RX_DEVICE_Capabilities(void) {
  ++probes; return (ReceiverCapabilities){.si = hasSi, .broadcast = !hasSi, .ssb_patch = hasSi};
}
void RX_DEVICE_Init(void) {}

void testFrequencyInput(void);

int main(int argc, char **argv) {
  (void)argv;
  hasSi = argc == 1;
  RECEIVER_Init();
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
  sampleOpen = false; RECEIVER_UpdateSquelch();
  assert(!gReceiver.open && mutes == before + 1);
  RECEIVER_UpdateSquelch(); assert(mutes == before + 1);
  // Watch routing commits the selected plan without retuning.
  RxPlan bk = {RX_DEVICE_BK, 438500000u, {RX_NFM, 12500, 12000, 1, 0}};
  assert(!RECEIVER_Select(&bk, 1u << RX_DEVICE_SI, true));
  assert(RECEIVER_Select(&bk, (1u << RX_DEVICE_SI) | (1u << RX_DEVICE_BK), true));
  assert(gReceiver.plan.frequency_hz == 438500000u && gReceiver.open && hardwareApplies == 2);
  testFrequencyInput();
  return 0;
}
