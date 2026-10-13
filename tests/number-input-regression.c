#include "ui/number-input.h"
#include <assert.h>
#include <string.h>

static NumberInput input;
static uint32_t value;
static NumberInputResult key(KEY_Code_t k) {
  return NUMBER_Key(&input, k, KEY_RELEASED, &value);
}
static void enter(const char *text, uint32_t expected) {
  NUMBER_Begin(&input, 150000, 1340000000, 1000000, 0, false);
  for (const char *c=text; *c; ++c) key(*c=='.' ? KEY_STAR : KEY_0+*c-'0');
  assert(key(KEY_MENU)==NUMBER_ACCEPTED && value==expected);
}
void testFrequencyInput(void) {
  enter("438.500", 438500000);
  enter("438500", 438500000);
  enter("95.92500", 95925000);
  enter("145.99999", 145999990);
  enter("95.925001", 95925001);
  enter("1340.00000", 1340000000);
  enter("0.150", 150000);
  enter(".150", 150000);
  NUMBER_Begin(&input, 150000, 1340000000, 1000000, 0, false);
  assert(key(KEY_MENU)==NUMBER_EDITING && input.active && input.invalid);
  key(KEY_0); key(KEY_MENU);
  assert(input.active && input.invalid);
  assert(key(KEY_EXIT)==NUMBER_EDITING && !input.length);
  assert(key(KEY_EXIT)==NUMBER_CANCELLED);
  NUMBER_Begin(&input, 0, 1340000000, 1000000, 0, false);
  const char *over="1340.000001";
  for (const char *c=over; *c; ++c) key(*c=='.' ? KEY_STAR : KEY_0+*c-'0');
  assert(key(KEY_MENU)==NUMBER_EDITING && input.invalid);
  assert(NUMBER_Key(&input, KEY_EXIT, KEY_LONG_PRESSED, &value)==NUMBER_CANCELLED);
  NUMBER_Begin(&input, 500, 860, 100, 824, true);
  assert(!strcmp(input.text,"8.24"));
  key(KEY_7); key(KEY_STAR); key(KEY_8); key(KEY_6);
  assert(!strcmp(input.text,"7.86"));
  assert(key(KEY_PTT)==NUMBER_EDITING && input.active);
  assert(key(KEY_F)==NUMBER_EDITING && input.active);
  assert(key(KEY_MENU)==NUMBER_ACCEPTED && value==786);
  NUMBER_Begin(&input, 0, UINT32_MAX, 1, 0, false);
  const char *overflow="4294967296";
  for (const char *c=overflow; *c; ++c) key(KEY_0+*c-'0');
  assert(key(KEY_MENU)==NUMBER_EDITING && input.invalid);
}
