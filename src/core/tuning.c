#include "core/tuning.h"

bool TUNING_Resolve(const RadioSlot *stored, const UserChannel *channel,
                    RadioSlot *effective) {
  if (!stored || !effective) return false;
  *effective = *stored;
  if (stored->source == RADIO_SOURCE_VFO) return true;
  if (stored->source != RADIO_SOURCE_CHANNEL || !channel) return false;
  uint8_t band = RADIO_MODEL_BandAt(channel->frequency_hz);
  if (band == UINT8_MAX) return false;
  effective->active_band = band;
  effective->current = (BandState){channel->frequency_hz, channel->rx};
  effective->tx = channel->tx;
  return true;
}

RxResult TUNING_ExactPlan(uint32_t hz, RadioParameters parameters,
                          ReceiverCapabilities caps, RxPlan *plan) {
  RxResult result = RADIO_MODEL_Plan(hz, parameters, caps, plan);
  if (result == RX_OK && plan->frequency_hz != hz)
    return RX_INVALID_PARAMETERS;
  return result;
}

bool TUNING_StepVfo(const RadioSlot *slot,
                    const RadioParameters presets[RADIO_STATE_BANDS], bool up,
                    uint32_t *hz, RadioParameters *parameters) {
  if (!slot || slot->source != RADIO_SOURCE_VFO || !presets || !hz || !parameters)
    return false;
  uint32_t target = RADIO_MODEL_Step(slot->current.frequency_hz,
                                     slot->current.parameters.step_hz, up);
  uint8_t band = RADIO_MODEL_BandAt(target);
  if (band == UINT8_MAX) return false;
  *hz = target;
  *parameters = band == slot->active_band
      ? slot->current.parameters : presets[band];
  return true;
}
