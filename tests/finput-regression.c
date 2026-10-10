#include "../src/apps/finput.c"
#include <assert.h>

bool gRedrawScreen;
bool APPS_exit(void) { return true; }
static uint32_t submitted;
static void onSubmit(uint32_t first, uint32_t second) {
  (void)second;
  submitted = first;
}
static void enter(const char *text, uint32_t expected) {
  gFInputValue1 = 0;
  gFInputCallback = onSubmit;
  submitted = 0;
  FINPUT_setup(0, 134000000, UNIT_MHZ, false);
  FINPUT_init();
  for (const char *c = text; *c; ++c)
    FINPUT_key(*c == '.' ? KEY_STAR : KEY_0 + *c - '0', KEY_RELEASED);
  FINPUT_key(KEY_MENU, KEY_RELEASED);
  assert(submitted == expected);
}
void testFrequencyInput(void) {
  enter("438.500", 43850000);
  enter("438500", 43850000); // Automatic decimal point.
  enter("95.92500", 9592500);
  enter("145.99999", 14599999);
  enter("95.925001", 9592500); // Truncate below the 10 Hz resolution.
  enter("1340.00000", 134000000);
}
