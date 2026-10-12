#ifndef UI_COMPONENT_VFO_H
#define UI_COMPONENT_VFO_H
#include "core/radio-model.h"
#define UI_VFO_HEIGHT 20
// Each visible VFO owns its view and scrolling state; no receiver globals.
typedef struct {
  uint32_t frequency_hz;
  RxMode mode;
  const char *band_short, *name, *rds, *status;
  char id;
  bool selected, changed, signal_valid;
  uint8_t signal_level;
} UI_VfoView;
typedef struct {
  char label[128];
  uint32_t started;
  uint16_t offset, overflow;
} UI_VfoState;
void UI_VfoRender(uint8_t y, const UI_VfoView *view, UI_VfoState *state, uint32_t now);
bool UI_VfoUpdate(UI_VfoState *state, uint32_t now);
#endif
