#include "core/radio-state.h"

static bool parametersValid(const RadioParameters *p) {
  return (unsigned)p->mode < RX_MODE_COUNT && p->step_hz &&
         p->bandwidth_hz <= UINT16_MAX && p->squelch <= 10 &&
         TONE_Valid(p->rx_tone) && (!p->rx_tone || p->mode == RX_NFM);
}
bool RADIO_VFO_Valid(const VfoConfig *vfo) {
  if (!vfo || vfo->active_band >= radioBandCount ||
      RADIO_MODEL_BandAt(vfo->current.frequency_hz) != vfo->active_band ||
      !parametersValid(&vfo->current.parameters) ||
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
      state->vfo_count < 2 || state->vfo_count > RADIO_VFO_CAPACITY ||
      state->selected_vfo >= state->vfo_count) return false;
  uint8_t watchers = 0;
  for (uint8_t id = 0; id < RADIO_VFO_CAPACITY; ++id) {
    if (!RADIO_VFO_Valid(&state->vfos[id]) ||
        (id>=state->vfo_count && state->vfos[id].watch)) return false;
    if (id < state->vfo_count && state->vfos[id].watch) ++watchers;
  }
  if (watchers > RADIO_WATCH_LIMIT) return false;
  for (uint8_t i = 0; i < radioBandCount; ++i)
    if (!parametersValid(&state->presets[i])) return false;
  return true;
}
void RADIO_STATE_InitVfo(RadioUserState *state, uint8_t id, uint8_t band) {
  if (!state || id >= RADIO_VFO_CAPACITY || band >= radioBandCount) return;
  VfoConfig *vfo = &state->vfos[id];
  *vfo = (VfoConfig){0};
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
  state->vfo_count = 2;
  for (uint8_t id=0; id<RADIO_VFO_CAPACITY; ++id) {
    RADIO_STATE_InitVfo(state, id, id%2 ? 6 : 5);
    state->vfos[id].watch = id < 2;
  }
  return RADIO_STATE_Valid(state);
}
bool RADIO_VFO_Set(VfoConfig *vfo, uint32_t hz, RadioParameters p) {
  uint8_t band = RADIO_MODEL_BandAt(hz);
  if (!vfo || band == UINT8_MAX || !parametersValid(&p)) return false;
  vfo->active_band = band;
  vfo->current = (BandState){hz, p};
  vfo->bands[band] = vfo->current;
  return true;
}
bool RADIO_STATE_Set(RadioUserState *state, uint8_t id, uint32_t hz,
                     RadioParameters p) {
  return state && id < state->vfo_count &&
         RADIO_VFO_Set(&state->vfos[id], hz, p);
}
bool RADIO_STATE_SelectBand(RadioUserState *state, uint8_t id, uint8_t band) {
  if (!RADIO_STATE_Valid(state) || id >= state->vfo_count || band >= radioBandCount)
    return false;
  VfoConfig *vfo = &state->vfos[id];
  return RADIO_VFO_Set(vfo, vfo->bands[band].frequency_hz, state->presets[band]);
}

bool RADIO_STATE_SetCount(RadioUserState *state, uint8_t count) {
  if (!state || count<2 || count>RADIO_VFO_CAPACITY) return false;
  uint8_t watchers=0;
  for (uint8_t id=0; id<count; ++id) if (state->vfos[id].watch) ++watchers;
  if (watchers>RADIO_WATCH_LIMIT) return false;
  for (uint8_t id=count; id<RADIO_VFO_CAPACITY; ++id) state->vfos[id].watch=false;
  state->vfo_count=count;
  if (state->selected_vfo>=count) state->selected_vfo=0;
  return true;
}
bool RADIO_STATE_SetWatch(RadioUserState *state, uint8_t id, bool enabled) {
  if (!state || id>=state->vfo_count) return false;
  unsigned others=0;
  for (uint8_t i=0; i<state->vfo_count; ++i)
    if (i!=id && state->vfos[i].watch) ++others;
  if (enabled && others>=RADIO_WATCH_LIMIT) return false;
  state->vfos[id].watch=enabled;
  return true;
}
uint8_t RADIO_STATE_NextVfo(const RadioUserState *state) {
  return state && state->vfo_count ? (state->selected_vfo+1u)%state->vfo_count : 0;
}
uint8_t RADIO_STATE_VisibleFirst(const RadioUserState *state) {
  if (!state || state->vfo_count<2) return 0;
  uint8_t first=(state->selected_vfo/RADIO_VFO_VISIBLE)*RADIO_VFO_VISIBLE;
  return first+RADIO_VFO_VISIBLE>state->vfo_count ? state->vfo_count-RADIO_VFO_VISIBLE : first;
}
