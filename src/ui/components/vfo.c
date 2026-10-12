#include "ui/components/vfo.h"
#include "ui/components/signal.h"
#include "ui/components/frequency.h"
#include "ui/graphics.h"
#include <string.h>

bool UI_VfoUpdate(UI_VfoState *state, uint32_t now) {
  if (!state->overflow) return false;
  uint32_t phase = (now-state->started)%(2400u+state->overflow*150u);
  uint16_t offset = phase < 1200 ? 0 : phase < 1200u+state->overflow*150u
                                    ? (phase-1200)/150 : state->overflow;
  if (offset == state->offset) return false;
  state->offset = offset;
  return true;
}

void UI_VfoRender(uint8_t y, const UI_VfoView *view, UI_VfoState *state, uint32_t now) {
  if (!view || !state || y+UI_VFO_HEIGHT > LCD_HEIGHT) return;
  char label[128], frequency[16];
  const char *name = view->name ? view->name : "";
  const char *rds = view->rds ? view->rds : "";
  snprintf(label, sizeof(label), "%s%s%s", name, *name && *rds ? " / " : "", rds);
  if (strcmp(label, state->label)) {
    strcpy(state->label, label);
    state->started = now;
    state->offset = 0;
  }
  const char *unit = UI_FormatFrequency(view->frequency_hz, frequency);
  const char *mode = (unsigned)view->mode < RX_MODE_COUNT ? rxModeNames[view->mode] : "--";
  uint16_t columnWidth = UI_SmallTextWidth(mode);
  uint16_t unitWidth = UI_SmallTextWidth(unit);
  if (unitWidth > columnWidth) columnWidth = unitWidth;
  uint8_t frequencyRight = LCD_WIDTH-columnWidth;
  const uint8_t labelX = 12;
  uint8_t labelWidth = frequencyRight-labelX;
  uint16_t textWidth = UI_MediumBoldTextWidth(label);
  state->overflow = textWidth > labelWidth ? textWidth-labelWidth : 0;
  if (!state->overflow) state->offset = 0;
  UI_VfoUpdate(state, now);
  FillRect(0, y, LCD_WIDTH, UI_VFO_HEIGHT, C_CLEAR);
  if (view->selected) {
    DrawVLine(0, y+2, 5, C_FILL);
    DrawVLine(1, y+3, 3, C_FILL);
    PutPixel(2, y+4, C_FILL);
  }
  PrintSmall(4, y+6, "%c", view->id ? view->id : 'A');
  if (view->changed) PrintSmall(8, y+6, "*");
  UI_SmallTextWindow(0, y+15, 11, view->band_short ? view->band_short : "", 0);
  if (*state->label) {
    UI_MediumBoldTextWindow(labelX, y+6, labelWidth, state->label, state->offset);
    PrintMediumBoldEx(frequencyRight, y+15, POS_R, C_FILL, "%s", frequency);
  } else {
    UI_CompactFrequencyDigits(frequencyRight, y+14, frequency);
  }
  PrintSmallEx(128, y+6, POS_R, C_FILL, "%s", mode);
  PrintSmallEx(128, y+15, POS_R, C_FILL, "%s", unit);
  UI_SignalBar(0, y+17, view->signal_valid ? view->signal_level : 0);
  if (view->status && *view->status)
    PrintSmallEx(127, y+19, POS_R, C_FILL, "%s", view->status);
}
