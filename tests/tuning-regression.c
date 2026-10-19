#include "core/tuning.h"
#include <assert.h>
#include <string.h>

int main(void) {
  RadioUserState state;
  assert(RADIO_STATE_Default(&state));
  RadioSlot stored = state.slots[0], effective;
  stored.current.frequency_hz = 145500000u;
  stored.current.parameters = (RadioParameters){RX_NFM, 12500, 12000, 4, 0};
  stored.active_band = RADIO_MODEL_BandAt(stored.current.frequency_hz);
  assert(TUNING_Resolve(&stored, NULL, &effective));
  assert(!memcmp(&stored, &effective, sizeof(stored)));

  uint32_t hz;
  RadioParameters params;
  assert(TUNING_StepVfo(&stored, state.presets, true, &hz, &params));
  assert(hz == 145512500u);
  assert(params.mode == RX_NFM && params.step_hz == 12500u);

  UserChannel channel = {
    .frequency_hz = 438500000u,
    .rx = {RX_NFM, 12500, 12000, 4, 0},
    .name = "Saved"
  };
  stored.source = RADIO_SOURCE_CHANNEL;
  stored.channel_number = 12;
  assert(!TUNING_Resolve(&stored, NULL, &effective));
  assert(TUNING_Resolve(&stored, &channel, &effective));
  assert(effective.source == RADIO_SOURCE_CHANNEL);
  assert(effective.current.frequency_hz == channel.frequency_hz);
  assert(stored.current.frequency_hz == 145500000u);
  assert(!TUNING_StepVfo(&stored, state.presets, true, &hz, &params));
  channel.frequency_hz = RADIO_MAX_HZ+1u;
  assert(!TUNING_Resolve(&stored, &channel, &effective));

  stored.source = RADIO_SOURCE_VFO;
  assert(TUNING_Resolve(&stored, NULL, &effective));
  assert(effective.current.frequency_hz == 145500000u);
  RxPlan plan;
  ReceiverCapabilities caps = {true, true, false, true, RX_BK_BANDWIDTH_ALL};
  assert(TUNING_ExactPlan(145500000u,
      (RadioParameters){RX_NFM, 12500, 12000, 4, 0}, caps, &plan) == RX_OK);
  assert(plan.frequency_hz == 145500000u);
  assert(TUNING_ExactPlan(0u,
      (RadioParameters){RX_NFM, 12500, 12000, 4, 0}, caps, &plan) != RX_OK);
}
