#include "ui/components/signal.h"
#include "core/radio.h"
#include "core/measurements.h"


// Equal-height segments. The BK4819 value uses the existing approximate
// S calibration; other receivers show relative RF strength, not S units.
uint8_t UI_SMeterLevel(void) {
  enum { SEGMENTS = 14 };
  uint8_t level = 0;

  if (ctx->radio_type == RADIO_BK4819) {
    if (vfo->msm.rssi) {
      int16_t dBm = Rssi2DBm(vfo->msm.rssi) + BK4819_GetAttenuation();
      level = DBm2S(dBm, ctx->frequency >= 300 * MHZ);
    }
  } else {
    level = ConvertDomain(vfo->msm.rssi, 0, 75, 0, SEGMENTS);
  }

  return level > SEGMENTS ? SEGMENTS : level;
}

void UI_SignalIcon(uint8_t x, uint8_t baseline, uint8_t level) {
  if (level > 14) level = 14;
  uint8_t bars = (level*8+13)/14;
  for (uint8_t i = 0; i < 8; ++i) {
    if (i < bars) FillRect(x+i*3, baseline-i, 2, i+1, C_FILL);
    else DrawHLine(x+i*3, baseline, 2, C_FILL);
  }
}


