#include "core/radio-state.h"
#include "core/channel-model.h"

static bool parametersValid(const RadioParameters *p) {
  return (unsigned)p->mode < RX_MODE_COUNT && p->step_hz &&
         p->bandwidth_hz <= UINT16_MAX && p->squelch <= 10 &&
         TONE_Valid(p->rx_tone) && (!p->rx_tone || p->mode == RX_NFM);
}
bool RADIO_SLOT_Valid(const RadioSlot *slot) {
  if (!slot || slot->active_band >= radioBandCount ||
      RADIO_MODEL_BandAt(slot->current.frequency_hz) != slot->active_band ||
      !parametersValid(&slot->current.parameters) ||
      (unsigned)slot->source > RADIO_SOURCE_CHANNEL ||
      slot->channel_number > USER_CHANNEL_COUNT ||
      (slot->source == RADIO_SOURCE_CHANNEL && !slot->channel_number) ||
      (unsigned)slot->tx.power > TX_POWER_HIGH || !TONE_Valid(slot->tx.tone) ||
      !TX_MODEL_ConfigValid(&slot->tx)) return false;
  for (uint8_t i = 0; i < radioBandCount; ++i) {
    const BandState *band = &slot->bands[i];
    if (band->frequency_hz < radioBands[i].start_hz ||
        band->frequency_hz >= radioBands[i].end_hz ||
        !parametersValid(&band->parameters)) return false;
  }
  return true;
}
bool RADIO_STATE_Valid(const RadioUserState *state) {
  if (!state || radioBandCount != RADIO_STATE_BANDS ||
      state->slot_count < 1 || state->slot_count > RADIO_SLOT_CAPACITY ||
      state->selected_slot >= state->slot_count) return false;
  for (uint8_t id = 0; id < RADIO_SLOT_CAPACITY; ++id)
    if (!RADIO_SLOT_Valid(&state->slots[id])) return false;
  for (uint8_t i = 0; i < radioBandCount; ++i)
    if (!parametersValid(&state->presets[i])) return false;
  return true;
}
void RADIO_STATE_InitSlot(RadioUserState *state, uint8_t id, uint8_t band) {
  if (!state || id >= RADIO_SLOT_CAPACITY || band >= radioBandCount) return;
  RadioSlot *slot = &state->slots[id];
  *slot = (RadioSlot){0};
  for (uint8_t i = 0; i < radioBandCount; ++i)
    slot->bands[i] = (BandState){radioBands[i].initial_hz, state->presets[i]};
  slot->active_band = band;
  slot->current = slot->bands[band];
}
bool RADIO_STATE_Default(RadioUserState *state) {
  if (!state || radioBandCount != RADIO_STATE_BANDS) return false;
  *state = (RadioUserState){0};
  for (uint8_t i = 0; i < radioBandCount; ++i)
    state->presets[i] = radioBands[i].preset;
  state->slot_count = 2;
  for (uint8_t id=0; id<RADIO_SLOT_CAPACITY; ++id)
    RADIO_STATE_InitSlot(state, id, id%2 ? 6 : 5);
  return RADIO_STATE_Valid(state);
}
bool RADIO_SLOT_SetVfo(RadioSlot *slot, uint32_t hz, RadioParameters p) {
  uint8_t band = RADIO_MODEL_BandAt(hz);
  if (!slot || band == UINT8_MAX || !parametersValid(&p)) return false;
  slot->active_band = band;
  slot->current = (BandState){hz, p};
  slot->bands[band] = slot->current;
  return true;
}
bool RADIO_STATE_Set(RadioUserState *state, uint8_t id, uint32_t hz,
                     RadioParameters p) {
  return state && id < state->slot_count &&
         RADIO_SLOT_SetVfo(&state->slots[id], hz, p);
}
bool RADIO_STATE_SelectBand(RadioUserState *state, uint8_t id, uint8_t band) {
  if (!RADIO_STATE_Valid(state) || id >= state->slot_count || band >= radioBandCount)
    return false;
  RadioSlot *slot = &state->slots[id];
  return RADIO_SLOT_SetVfo(slot, slot->bands[band].frequency_hz, state->presets[band]);
}

bool RADIO_STATE_SetCount(RadioUserState *state, uint8_t count) {
  if (!state || count<1 || count>RADIO_SLOT_CAPACITY) return false;
  state->slot_count=count;
  if (state->selected_slot>=count) state->selected_slot=0;
  return true;
}
uint8_t RADIO_STATE_NextSlot(const RadioUserState *state) {
  return state && state->slot_count ? (state->selected_slot+1u)%state->slot_count : 0;
}
uint8_t RADIO_STATE_VisibleFirst(const RadioUserState *state) {
  if (!state || state->slot_count<2) return 0;
  uint8_t first=(state->selected_slot/RADIO_SLOT_VISIBLE)*RADIO_SLOT_VISIBLE;
  return first+RADIO_SLOT_VISIBLE>state->slot_count ? state->slot_count-RADIO_SLOT_VISIBLE : first;
}
