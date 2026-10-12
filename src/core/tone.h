#ifndef RADIO_TONE_H
#define RADIO_TONE_H
#include "core/dcs.h"
#include <stdbool.h>
// Stable tone option catalogue: Off, 50 CTCSS, 104 DCS N, 104 DCS I.
// This is a user option, never a BK register/control word.
typedef uint16_t RadioTone;
#define RADIO_TONE_COUNT 259u
static inline bool TONE_Valid(RadioTone tone) { return tone < RADIO_TONE_COUNT; }
static inline DCS_CodeType_t TONE_Type(RadioTone tone) {
  return !tone ? CODE_TYPE_OFF : tone <= 50 ? CODE_TYPE_CONTINUOUS_TONE :
         tone <= 154 ? CODE_TYPE_DIGITAL : CODE_TYPE_REVERSE_DIGITAL;
}
static inline uint8_t TONE_Code(RadioTone tone) {
  return !tone ? 0 : tone <= 50 ? tone-1 : tone <= 154 ? tone-51 : tone-155;
}
static inline RadioTone TONE_Next(RadioTone tone, bool up) {
  return (tone + (up ? 1u : RADIO_TONE_COUNT-1u)) % RADIO_TONE_COUNT;
}
void TONE_Format(RadioTone tone, char text[24]);
#endif
