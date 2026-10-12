#include "ui/components/scan.h"
#include "core/radio.h"
#include "core/measurements.h"
#include "core/channels.h"
#include "ui/components/signal.h"

void UI_Scanlists(uint8_t baseX, uint8_t baseY, uint16_t sl) {
  for (uint8_t i = 0; i < 16; ++i) {
    bool isActive = (sl >> i) & 1;
    uint8_t xi = i & 7, yi = i >> 3;
    FillRect(baseX + xi * 3 + (xi > 3) * 2, baseY + yi * 3 + (yi && !isActive),
             2, 1 + isActive, C_INVERT);
  }
}

void UI_DrawLoot(const Loot *loot, uint8_t x, uint8_t y, TextPos pos) {
  char c = loot->blacklist ? '-' : loot->whitelist ? '+' : ' ';
  PrintMediumEx(x, y, pos, C_INVERT, "%c%u.%05u %c", c, loot->f / MHZ,
                loot->f % MHZ, loot->open ? '!' : ' ');
}

void UI_DisplayScanlists(uint32_t y) {
  char buf[17];
  ScanlistStr(gSettings.currentScanlist, buf);
  PrintMediumEx(LCD_XCENTER, y, POS_C, C_FILL, "%.4s %.4s %.4s %.4s", buf,
                buf + 4, buf + 8, buf + 12);
}

void UI_RenderScanScreen() {
  if (gScanlistSize) {
    uint32_t f = ctx ? ctx->frequency : 0;
    PrintMediumEx(LCD_XCENTER, 26, POS_C, C_FILL, "%u.%05u", f / MHZ, f % MHZ);
    if (vfo->is_open)
      UI_RSSIBar(28);
  } else {
    PrintMediumBoldEx(LCD_XCENTER, 18, POS_C, C_FILL, "Scanlist empty");
  }

  if (gLastActiveLoot) {
    UI_DrawLoot(gLastActiveLoot, LCD_XCENTER, 50, POS_C);
  }
  UI_DisplayScanlists(60);
}
