#include "drivers/bk48x9/bk48x9.h"
#include "drivers/bk48x9/internal.h"
#include "core/misc.h"

static uint8_t lastMode=UINT8_MAX;
void BK48X9_InvalidateRxMode(void) { lastMode=UINT8_MAX; }
void BK48X9_SetRxMode(bool am) {
  if (lastMode==(uint8_t)am) return;
  if (BK48X9_ConfigureDemodulator(am)) lastMode=(uint8_t)am;
}
