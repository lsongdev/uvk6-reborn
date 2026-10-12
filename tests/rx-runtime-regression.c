#include "../src/core/rx-runtime.h"
#include <assert.h>

typedef struct {
  uint32_t reject_hz;
  bool reject_all;
  unsigned attempts;
  unsigned stops;
  uint32_t history[8];
} FakeReceiver;

static bool apply(void *context, const RxPlan *plan) {
  FakeReceiver *rx = context;
  rx->history[rx->attempts++] = plan->frequency_hz;
  return !rx->reject_all && plan->frequency_hz != rx->reject_hz;
}
static void stop(void *context) {
  ((FakeReceiver *)context)->stops++;
}
static RxPlan at(uint32_t hz) {
  return (RxPlan){.device=RX_DEVICE_BK, .frequency_hz=hz,
                  .parameters={.mode=RX_NFM,.step_hz=12500,.bandwidth_hz=12000}};
}

int main(void) {
  RxRuntime runtime = {0};
  FakeReceiver rx = {0};
  RxPlan first = at(145500000), second = at(145512500);
  assert(RX_RuntimeTune(&runtime, &first, apply, stop, &rx) == RX_APPLIED);
  assert(runtime.active && runtime.current.frequency_hz == 145500000);

  rx.reject_hz = second.frequency_hz;
  assert(RX_RuntimeTune(&runtime, &second, apply, stop, &rx) == RX_RESTORED);
  assert(runtime.active && runtime.current.frequency_hz == 145500000);
  assert(rx.attempts == 3 && rx.history[1] == second.frequency_hz &&
         rx.history[2] == first.frequency_hz && rx.stops == 0);

  rx.reject_all = true;
  assert(RX_RuntimeTune(&runtime, &second, apply, stop, &rx) == RX_STOPPED);
  assert(!runtime.active && rx.attempts == 5 && rx.stops == 1);
  assert(RX_RuntimeTune(&runtime, &second, apply, stop, &rx) == RX_STOPPED);
  assert(rx.attempts == 6 && rx.stops == 2); // No stale recovery after stop.

  RX_RuntimeStop(&runtime, stop, &rx);
  assert(!runtime.active && rx.stops == 3);
  return 0;
}
