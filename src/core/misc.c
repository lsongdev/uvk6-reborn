#include "core/misc.h"
#include "drivers/bk4819/bk4819.h"
#include "external/printf/printf.h"




void mhzToS(char *buf, uint32_t f) {
  sprintf(buf, "%u.%05u", f / MHZ, f % MHZ);
}

void bkAttToS(char *buf, uint8_t v) {
  sprintf(buf, v == AUTO_GAIN_INDEX ? "Auto" : "%udB", GAIN_TABLE[v].gainDb);
}
