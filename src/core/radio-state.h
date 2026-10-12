#ifndef RADIO_STATE_H
#define RADIO_STATE_H

#include "core/tx-model.h"

// A fixed set of built-in bands. Changes to this table require a storage
// format version change, never reinterpretation of an old byte array.
#define RADIO_STATE_BANDS 9
#define RADIO_VFO_CAPACITY 4
#define RADIO_WATCH_LIMIT 2
#define RADIO_VFO_VISIBLE 2

typedef struct {
  uint32_t frequency_hz;
  RadioParameters parameters;
} BandState;

typedef struct {
  uint8_t active_band;
  BandState current;
  BandState bands[RADIO_STATE_BANDS];
  TxConfig tx;
  bool watch;
} VfoConfig;

typedef struct {
  uint8_t vfo_count, selected_vfo;
  bool watch_enabled;
  VfoConfig vfos[RADIO_VFO_CAPACITY];
  RadioParameters presets[RADIO_STATE_BANDS];
} RadioUserState;

bool RADIO_STATE_SetCount(RadioUserState *state, uint8_t count);
bool RADIO_STATE_SetWatch(RadioUserState *state, uint8_t id, bool enabled);
uint8_t RADIO_STATE_NextVfo(const RadioUserState *state);
uint8_t RADIO_STATE_VisibleFirst(const RadioUserState *state);
bool RADIO_STATE_Default(RadioUserState *state);
bool RADIO_STATE_SelectBand(RadioUserState *state, uint8_t id, uint8_t band);
bool RADIO_STATE_Set(RadioUserState *state, uint8_t id, uint32_t frequency_hz,
                     RadioParameters parameters);
bool RADIO_VFO_Set(VfoConfig *vfo, uint32_t frequency_hz, RadioParameters parameters);
bool RADIO_VFO_Valid(const VfoConfig *vfo);
void RADIO_STATE_InitVfo(RadioUserState *state, uint8_t id, uint8_t band);
bool RADIO_STATE_Valid(const RadioUserState *state);

#endif
