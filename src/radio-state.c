#include "radio-state.h"

bool RADIO_STATE_Valid(const RadioUserState *state) {
  if (!state || radioBandCount != RADIO_STATE_BANDS ||
      state->active_band >= radioBandCount)
    return false;
  if (RADIO_MODEL_BandAt(state->current.frequency_hz) != state->active_band ||
      (unsigned)state->current.parameters.mode >= RX_MODE_COUNT ||
      !state->current.parameters.step_hz ||
      state->current.parameters.squelch > 10 ||
      state->current.parameters.tx_enabled)
    return false;
  for (uint8_t i = 0; i < radioBandCount; ++i) {
    const BandState *band = &state->bands[i];
    if (band->frequency_hz < radioBands[i].start_hz ||
        band->frequency_hz >= radioBands[i].end_hz ||
        (unsigned)band->parameters.mode >= RX_MODE_COUNT ||
        !band->parameters.step_hz || band->parameters.squelch > 10 ||
        band->parameters.tx_enabled)
      return false;
  }
  return true;
}

bool RADIO_STATE_Default(RadioUserState *state) {
  if (!state || radioBandCount != RADIO_STATE_BANDS)
    return false;
  for (uint8_t i = 0; i < radioBandCount; ++i) {
    state->bands[i] = (BandState){radioBands[i].initial_hz,
                                   radioBands[i].preset};
  }
  state->active_band = 5; // 2m: 145.500 MHz, NFM.
  state->current = state->bands[state->active_band];
  return RADIO_STATE_Valid(state);
}

bool RADIO_STATE_Set(RadioUserState *state, uint32_t frequency_hz,
                     RadioParameters parameters) {
  uint8_t band = RADIO_MODEL_BandAt(frequency_hz);
  if (!state || !RADIO_STATE_Valid(state) || band == UINT8_MAX ||
      (unsigned)parameters.mode >= RX_MODE_COUNT || !parameters.step_hz ||
      parameters.squelch > 10 || parameters.tx_enabled)
    return false;
  // A saved successful tune becomes this band's last-used state.
  state->active_band = band;
  state->current = (BandState){frequency_hz, parameters};
  state->bands[band] = state->current;
  return true;
}

bool RADIO_STATE_Select(RadioUserState *state, uint8_t band) {
  if (!RADIO_STATE_Valid(state) || band >= radioBandCount)
    return false;
  state->active_band = band;
  state->current = state->bands[band];
  return true;
}
