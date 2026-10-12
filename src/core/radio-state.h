#ifndef RADIO_STATE_H
#define RADIO_STATE_H

#include "core/radio-model.h"

// A fixed set of built-in bands. Changes to this table require a storage
// format version change, never reinterpretation of an old byte array.
#define RADIO_STATE_BANDS 9

typedef struct {
  uint32_t frequency_hz;
  RadioParameters parameters;
} BandState;

typedef struct {
  uint8_t active_band;
  BandState current;
  BandState bands[RADIO_STATE_BANDS];
  RadioParameters presets[RADIO_STATE_BANDS];
} RadioUserState;

bool RADIO_STATE_Default(RadioUserState *state);
bool RADIO_STATE_Select(RadioUserState *state, uint8_t band);
bool RADIO_STATE_Set(RadioUserState *state, uint32_t frequency_hz,
                     RadioParameters parameters);
bool RADIO_STATE_Valid(const RadioUserState *state);

#endif
