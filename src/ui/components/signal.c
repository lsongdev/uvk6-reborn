#include "ui/components/signal.h"
#include "core/radio.h"
#include "core/measurements.h"

void UI_RSSIBar(uint8_t y) {
  uint16_t rssi = vfo->msm.rssi;
  uint8_t snr = vfo->msm.snr;
  if (!rssi)
    return;

  const uint8_t BAR_WIDTH = LCD_WIDTH - 24;
  const uint8_t BAR_BASE = y + 7;
  const bool isUhf = ctx->frequency >= 30 * MHZ;

  const uint8_t *r2dBm = rssi2s[isUhf];
  const uint8_t numTicks = 12;
  const int16_t DBM_MIN = -r2dBm[0];
  const int16_t DBM_MAX = -r2dBm[numTicks - 1];

  uint8_t att = ctx->radio_type == RADIO_BK4819 ? BK4819_GetAttenuation() : 0;
  int16_t dBm = Rssi2DBm(rssi) + att;
  uint8_t rssiW = ConvertDomain(dBm, DBM_MIN, DBM_MAX, 0, BAR_WIDTH);
  uint8_t barH = ConvertDomain(snr, 0, 30, 1, 6);

  FillRect(0, y, LCD_WIDTH, 8, C_CLEAR);

  PrintMediumEx(LCD_WIDTH - 1, BAR_BASE, POS_R, C_FILL, "%+3d", dBm);

  FillRect(0, BAR_BASE - barH, rssiW, barH, C_FILL);
  DrawHLine(0, BAR_BASE, BAR_WIDTH, C_FILL);

  for (uint8_t i = 1; i <= numTicks; i++) {
    dBm = -r2dBm[i - 1];
    uint8_t x = ConvertDomain(dBm, DBM_MIN, DBM_MAX, 0, BAR_WIDTH);
    uint8_t height = (i == 9) ? 4 : 2;
    FillRect(x, BAR_BASE - height, 1, height, C_INVERT);
  }
}

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

void UI_SMeter(uint8_t y) {
  enum { SEGMENTS = 14, SEGMENT_W = 6, SEGMENT_GAP = 1, BAR_X = 0 };
  uint8_t level = UI_SMeterLevel();
  if (level > SEGMENTS)
    level = SEGMENTS;
  for (uint8_t i = 0; i < SEGMENTS; ++i) {
    uint8_t x = BAR_X + i * (SEGMENT_W + SEGMENT_GAP);
    DrawRect(x, y, SEGMENT_W, 8, C_FILL);
    if (i < level)
      FillRect(x + 1, y + 1, SEGMENT_W - 2, 6, C_FILL);
  }
}

