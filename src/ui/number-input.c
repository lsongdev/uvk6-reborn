#include "ui/number-input.h"
#include "core/text.h"
#include <string.h>

static bool parse(const NumberInput *input, uint32_t *value) {
  uint32_t whole = 0, fraction = 0, place = input->scale;
  bool dot = false, digit = false;
  for (uint8_t i = 0; i < input->length; ++i) {
    char c = input->text[i];
    if (c == '.') { dot = true; continue; }
    uint32_t d = c-'0';
    digit = true;
    if (!dot) {
      if (whole > (UINT32_MAX-d)/10u) return false;
      whole = whole*10u+d;
    } else {
      place /= 10u;
      fraction += d*place;
    }
  }
  if (!digit || whole > (UINT32_MAX-fraction)/input->scale) return false;
  *value = whole*input->scale+fraction;
  return true;
}

void NUMBER_Begin(NumberInput *input, uint32_t min, uint32_t max,
                  uint32_t scale, uint32_t value, bool prefill) {
  *input = (NumberInput){.min=min, .max=max, .scale=scale, .active=true,
                         .replace=prefill};
  for (uint32_t n=scale; n>1; n/=10) ++input->decimals;
  if (prefill) {
    Text t = TEXT_Begin(input->text, sizeof(input->text));
    TEXT_Number(&t, value/scale, 1, 10);
    if (input->decimals) {
      TEXT_Char(&t, '.');
      TEXT_Number(&t, value%scale, input->decimals, 10);
    }
    input->length = strlen(input->text);
  }
}

NumberInputResult NUMBER_Key(NumberInput *input, KEY_Code_t key,
                             Key_State_t state, uint32_t *value) {
  if (state == KEY_LONG_PRESSED && key == KEY_EXIT) {
    input->active = false;
    return NUMBER_CANCELLED;
  }
  if (state != KEY_RELEASED) return NUMBER_EDITING;
  if (key == KEY_MENU) {
    if (!parse(input, value) || *value<input->min || *value>input->max) {
      input->invalid = true;
      return NUMBER_EDITING;
    }
    input->active = false;
    return NUMBER_ACCEPTED;
  }
  if (key == KEY_EXIT) {
    if (!input->length) {
      input->active = false;
      return NUMBER_CANCELLED;
    }
    input->text[--input->length] = 0;
    input->replace = input->invalid = false;
  } else if (key <= KEY_9 || key == KEY_STAR) {
    if (input->replace && key <= KEY_9) {
      input->length = 0;
      input->text[0] = 0;
      input->replace = false;
    }
    char *dot = strchr(input->text, '.');
    if (key == KEY_STAR) {
      if (!input->decimals || dot) return NUMBER_EDITING;
      if (!input->length) input->text[input->length++] = '0';
      input->text[input->length++] = '.';
    } else {
      if (input->length >= sizeof(input->text)-2 ||
          (dot && strlen(dot+1)>=input->decimals)) return NUMBER_EDITING;
      // Preserve keypad shorthand (438500 -> 438.500) without a fixed MHz width.
      uint32_t current;
      if (!dot && input->decimals && input->length &&
          (!parse(input, &current) ||
           key*input->scale > input->max ||
           current > (input->max-key*input->scale)/10u))
        input->text[input->length++] = '.';
      input->text[input->length++] = '0'+key;
    }
    input->text[input->length] = 0;
    input->invalid = false;
  }
  return NUMBER_EDITING;
}

void NUMBER_Text(const NumberInput *input, char out[18]) {
  Text t = TEXT_Begin(out, 18);
  TEXT_Add(&t, input->text);
  TEXT_Char(&t, '_');
}
