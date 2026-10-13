#ifndef CORE_TUNING_H
#define CORE_TUNING_H
#include "core/radio-state.h"
#include "core/channel-model.h"

// MR is resolved transiently: direct VFO memories remain untouched.
bool TUNING_Resolve(const RadioSlot *stored, const UserChannel *channel,
                    RadioSlot *effective);
// A saved source must tune exactly. Manual keypad input can require confirmation.
RxResult TUNING_ExactPlan(uint32_t hz, RadioParameters parameters,
                          ReceiverCapabilities caps, RxPlan *plan);
bool TUNING_StepVfo(const RadioSlot *slot,
                    const RadioParameters presets[RADIO_STATE_BANDS], bool up,
                    uint32_t *hz, RadioParameters *parameters);
#endif
