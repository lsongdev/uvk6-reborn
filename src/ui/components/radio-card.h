#ifndef UI_COMPONENT_RADIO_CARD_H
#define UI_COMPONENT_RADIO_CARD_H
#include "core/radio-model.h"
#define UI_RADIO_CARD_HEIGHT 28
#define UI_RADIO_CARD_NAME_DELAY_MS 1000u
#define UI_RADIO_CARD_SCROLL_PAUSE_MS 1200u
#define UI_RADIO_CARD_SCROLL_PIXEL_MS 150u
// Receiver state is explicit; a valid RSSI alone does not mean squelch is open.
typedef enum {
  UI_RADIO_CARD_IDLE, UI_RADIO_CARD_RECEIVING, UI_RADIO_CARD_TRANSMITTING,
  UI_RADIO_CARD_SEEKING, UI_RADIO_CARD_WATCHING, UI_RADIO_CARD_UNAVAILABLE, UI_RADIO_CARD_FAULT
} UI_RadioCardActivity;
// Each visible VFO owns its view and animation state; no receiver globals.
typedef struct {
  uint32_t frequency_hz, step_hz;
  RxMode mode;
  UI_RadioCardActivity activity;
  const char *band_name, *name, *rds, *power_label;
  const char *edit_text; // Native MHz draft; owns the body while editing.
  const char *header_message; // Temporarily replaces band text in the black header.
  char id;
  bool selected, changed, signal_valid;
  uint8_t signal_level; // Existing 0..14 relative/S mapping, rendered as 8 bars.
} UI_RadioCardView;
typedef struct {
  char label[128], band[64];
  uint32_t tuned_at, label_started, band_started, frequency_hz;
  uint16_t offset, overflow, band_offset, band_overflow, band_width;
  bool initialized, show_name, tuning;
} UI_RadioCardState;
// Restart the one-second frequency hold even when retuning to the same Hz.
void UI_RadioCardShowFrequency(UI_RadioCardState *state, uint32_t now);
void UI_RadioCardRender(uint8_t y, const UI_RadioCardView *view, UI_RadioCardState *state, uint32_t now);
bool UI_RadioCardUpdate(UI_RadioCardState *state, uint32_t now);
#endif
