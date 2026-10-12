#include "../src/core/bands.c"
#include <assert.h>
#include <string.h>
void CHANNELS_Load(int16_t index, CH *channel) {
  (void)index; (void)channel;
  assert(false); // These tests do not install any EEPROM band records.
}
uint32_t DeltaF(uint32_t first, uint32_t second) {
  return first > second ? first - second : second - first;
}
void testBandNames(void) {
  gCurrentBand = defaultBand;
  assert(strcmp(BANDS_GetDisplayName(9592500), "FM Radio") == 0);
  assert(strcmp(BANDS_GetDisplayName(43850000), "70cm") == 0);
  assert(strcmp(BANDS_GetDisplayName(14550000), "2m") == 0);
  gCurrentBand.meta.type = TYPE_BAND;
  gCurrentBand.rxF = 43000000;
  gCurrentBand.txF = 44000000;
  strcpy(gCurrentBand.name, "Custom");
  assert(strcmp(BANDS_GetDisplayName(43850000), "Custom") == 0);
  assert(strcmp(BANDS_GetDisplayName(9592500), "FM Radio") == 0);
}
