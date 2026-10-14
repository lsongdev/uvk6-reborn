#ifndef UI_RADIO_CARD_H
#define UI_RADIO_CARD_H
#include "core/radio-model.h"
#define UI_RADIO_CARD_HEIGHT 28
// Receiver state is explicit; a valid RSSI alone does not mean squelch is open.
typedef enum {
  UI_RADIO_CARD_IDLE, UI_RADIO_CARD_RECEIVING, UI_RADIO_CARD_TRANSMITTING,
  UI_RADIO_CARD_SEEKING, UI_RADIO_CARD_UNAVAILABLE, UI_RADIO_CARD_FAULT
} UI_RadioCardActivity;
// Everything one card shows; the card keeps no state and never animates.
typedef struct {
  uint32_t frequency_hz, step_hz;
  RxMode mode;
  UI_RadioCardActivity activity;
  const char *band_name, *power_label;
  const char *name;       // Channel name or RDS PS: shown large in the body.
  const char *radio_text; // Fresh RDS RT: owns the header, frequency included.
  const char *edit_text;  // Draft in edit_unit; owns the body while editing.
  const char *edit_unit;  // "MHz" or "CH"; NULL means MHz.
  const char *header_message; // Temporarily replaces band text in the black header.
  const char *mode_text;  // Replaces the mode name, e.g. the FC scope.
  char id;
  bool selected, changed, signal_valid;
  uint8_t signal_level; // Existing 0..14 relative/S mapping, rendered as 8 bars.
} UI_RadioCardView;
void UI_RadioCardRender(uint8_t y, const UI_RadioCardView *view);
#endif
