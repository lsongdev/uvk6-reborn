#include "store/preferences.h"
#include "store/device-config.h"

uint8_t BL_TIME_VALUES[7] = {0, 5, 10, 20, 60, 120, 255};
const char *BL_SQL_MODE_NAMES[3] = {"Off", "Timed", "Receiving"};

uint32_t PREFERENCES_FilterBound(void) {
  return gDeviceConfig.preferences.bound_240_280 ? PREFERENCES_FILTER_BOUND_280
                                                 : PREFERENCES_FILTER_BOUND_240;
}
