#ifndef RADIO_STATE_H
#define RADIO_STATE_H

#include "core/tx-model.h"

// A fixed set of built-in bands. Changes to this table require a storage
// format version change, never reinterpretation of an old byte array.
#define RADIO_STATE_BANDS 9
#define RADIO_SLOT_CAPACITY 4
#define RADIO_WATCH_LIMIT 2
#define RADIO_SLOT_VISIBLE 2

typedef enum { RADIO_SOURCE_VFO, RADIO_SOURCE_CHANNEL } RadioSource;

typedef struct {
  uint32_t frequency_hz;
  RadioParameters parameters;
} BandState;

// A/B/C/D are independent radio slots, not four permanently active VFOs.
// The "current"/"bands" fields retain the slot's last direct-tuning memory
// even while source==RADIO_SOURCE_CHANNEL; MR resolves the active RX from storage.
typedef struct {
  uint8_t active_band;
  BandState current;
  BandState bands[RADIO_STATE_BANDS];
  TxConfig tx;
  bool watch;
  RadioSource source;
  uint8_t channel_number;
} RadioSlot;

typedef struct {
  uint8_t slot_count, selected_slot;
  bool watch_enabled;
  RadioSlot vfos[RADIO_SLOT_CAPACITY];
  RadioParameters presets[RADIO_STATE_BANDS];
} RadioUserState;

bool RADIO_STATE_SetCount(RadioUserState *state, uint8_t count);
bool RADIO_STATE_SetWatch(RadioUserState *state, uint8_t id, bool enabled);
uint8_t RADIO_STATE_NextSlot(const RadioUserState *state);
uint8_t RADIO_STATE_VisibleFirst(const RadioUserState *state);
bool RADIO_STATE_Default(RadioUserState *state);
bool RADIO_STATE_SelectBand(RadioUserState *state, uint8_t id, uint8_t band);
bool RADIO_STATE_Set(RadioUserState *state, uint8_t id, uint32_t frequency_hz,
                     RadioParameters parameters);
bool RADIO_SLOT_SetVfo(RadioSlot *vfo, uint32_t frequency_hz, RadioParameters parameters);
bool RADIO_SLOT_Valid(const RadioSlot *vfo);
void RADIO_STATE_InitSlot(RadioUserState *state, uint8_t id, uint8_t band);
bool RADIO_STATE_Valid(const RadioUserState *state);

#endif
