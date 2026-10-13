#ifndef UI_NUMBER_INPUT_H
#define UI_NUMBER_INPUT_H
#include "hal/keyboard.h"

// Decimal draft in caller-owned storage; scale and bounds use native units.
typedef struct {
  char text[16];
  uint32_t min, max, scale;
  uint8_t length, decimals;
  bool active, replace, invalid;
} NumberInput;
typedef enum { NUMBER_EDITING, NUMBER_ACCEPTED, NUMBER_CANCELLED } NumberInputResult;
void NUMBER_Begin(NumberInput *input, uint32_t min, uint32_t max,
                  uint32_t scale, uint32_t value, bool prefill);
NumberInputResult NUMBER_Key(NumberInput *input, KEY_Code_t key,
                             Key_State_t state, uint32_t *value);
void NUMBER_Text(const NumberInput *input, char out[18]);
#endif
