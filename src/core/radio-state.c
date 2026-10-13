#include "core/radio-state.h"

static bool parametersValid(const RadioParameters *p) {
  return (unsigned)p->mode < RX_MODE_COUNT && p->step_hz &&
         p->bandwidth_hz <= UINT16_MAX && p->squelch <= 10 &&
         TONE_Valid(p->rx_tone) && (!p->rx_tone || p->mode == RX_NFM);
}
bool RADIO_SLOT_Valid(const RadioSlot *vfo) {
  if (!vfo || vfo->active_band >= radioBandCount ||
      RADIO_MODEL_BandAt(vfo->current.frequency_hz) != vfo->active_band ||
      !parametersValid(&vfo->current.parameters) ||
      (unsigned)vfo->source > RADIO_SOURCE_CHANNEL ||
      vfo->channel_number > 128 ||
      (vfo->source == RADIO_SOURCE_CHANNEL && !vfo->channel_number) ||
      (unsigned)vfo->tx.power > TX_POWER_HIGH || !TONE_Valid(vfo->tx.tone) ||
      !TX_MODEL_FrequencyValid(vfo->tx.frequency_hz)) return false;
  for (uint8_t i = 0; i < radioBandCount; ++i) {
    const BandState *band = &vfo->bands[i];
    if (band->frequency_hz < radioBands[i].start_hz ||
        band->frequency_hz >= radioBands[i].end_hz ||
        !parametersValid(&band->parameters)) return false;
  }
  return true;
}
bool RADIO_STATE_Valid(const RadioUserState *state) {
  if (!state || radioBandCount != RADIO_STATE_BANDS ||
      state->slot_count < 2 || state->slot_count > RADIO_SLOT_CAPACITY ||
      state->selected_slot >= state->slot_count) return false;
  uint8_t watchers = 0;
  for (uint8_t id = 0; id < RADIO_SLOT_CAPACITY; ++id) {
    if (!RADIO_SLOT_Valid(&state->slots[id]) ||
        (id>=state->slot_count && state->slots[id].watch)) return false;
    if (id < state->slot_count && state->slots[id].watch) ++watchers;
  }
  if (watchers > RADIO_WATCH_LIMIT) return false;
  for (uint8_t i = 0; i < radioBandCount; ++i)
    if (!parametersValid(&state->presets[i])) return false;
  return true;
}
void RADIO_STATE_InitSlot(RadioUserState *state, uint8_t id, uint8_t band) {
  if (!state || id >= RADIO_SLOT_CAPACITY || band >= radioBandCount) return;
  RadioSlot *vfo = &state->slots[id];
  *vfo = (RadioSlot){0};
  for (uint8_t i = 0; i < radioBandCount; ++i)
    vfo->bands[i] = (BandState){radioBands[i].initial_hz, state->presets[i]};
  vfo->active_band = band;
  vfo->current = vfo->bands[band];
}
bool RADIO_STATE_Default(RadioUserState *state) {
  if (!state || radioBandCount != RADIO_STATE_BANDS) return false;
  *state = (RadioUserState){0};
  for (uint8_t i = 0; i < radioBandCount; ++i)
    state->presets[i] = radioBands[i].preset;
  state->slot_count = 2;
  for (uint8_t id=0; id<RADIO_SLOT_CAPACITY; ++id) {
    RADIO_STATE_InitSlot(state, id, id%2 ? 6 : 5);
    state->slots[id].watch = id < 2;
  }
  return RADIO_STATE_Valid(state);
}
bool RADIO_SLOT_SetVfo(RadioSlot *vfo, uint32_t hz, RadioParameters p) {
  uint8_t band = RADIO_MODEL_BandAt(hz);
  if (!vfo || band == UINT8_MAX || !parametersValid(&p)) return false;
  vfo->active_band = band;
  vfo->current = (BandState){hz, p};
  vfo->bands[band] = vfo->current;
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
  RadioSlot *vfo = &state->slots[id];
  return RADIO_SLOT_SetVfo(vfo, vfo->bands[band].frequency_hz, state->presets[band]);
}

bool RADIO_STATE_SetCount(RadioUserState *state, uint8_t count) {
  if (!state || count<2 || count>RADIO_SLOT_CAPACITY) return false;
  uint8_t watchers=0;
  for (uint8_t id=0; id<count; ++id) if (state->slots[id].watch) ++watchers;
  if (watchers>RADIO_WATCH_LIMIT) return false;
  for (uint8_t id=count; id<RADIO_SLOT_CAPACITY; ++id) state->slots[id].watch=false;
  state->slot_count=count;
  if (state->selected_slot>=count) state->selected_slot=0;
  return true;
}
bool RADIO_STATE_SetWatch(RadioUserState *state, uint8_t id, bool enabled) {
  if (!state || id>=state->slot_count) return false;
  unsigned others=0;
  for (uint8_t i=0; i<state->slot_count; ++i)
    if (i!=id && state->slots[i].watch) ++others;
  if (enabled && others>=RADIO_WATCH_LIMIT) return false;
  state->slots[id].watch=enabled;
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
