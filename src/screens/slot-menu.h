#ifndef SCREENS_SLOT_MENU_H
#define SCREENS_SLOT_MENU_H
#include "screens/screen.h"
#include "core/spectrum.h"

// Per-slot menu: RX parameters, TX settings, Reset to defaults and Save as
// channel. Item order matches RadioParameterField for the RX items.
enum {
  MENU_RX_MODE, MENU_RX_STEP, MENU_RX_BANDWIDTH, MENU_RX_SQUELCH, MENU_RX_TONE,
  MENU_TX_ENABLE, MENU_TX_POWER, MENU_TX_SHIFT,
  MENU_TX_OFFSET, // TX Offset for +/-, TX Freq for Split.
  MENU_TX_TONE,
#if UVK6_ENABLE_SPECTRUM
  MENU_SPECTRUM,
#endif
  MENU_RESET, MENU_SAVE_CHANNEL,
  MENU_ITEM_COUNT
};
extern const Screen slotMenuScreen;
#endif
