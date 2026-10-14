// Per-slot menu: RX parameters, TX settings, reset and Save as channel.
#include "ui/screens/slot-menu.h"
#include "ui/screens/save-channel.h"
#include "ui/screens/spectrum.h"
#include "radio/radio.h"
#include "core/text.h"
#include "core/tone.h"
#include "ui/frequency.h"
#include "ui/graphics.h"
#include "ui/number-input.h"
#include "ui/screens/statusline.h"

_Static_assert(MENU_RX_MODE == (int)RADIO_FIELD_MODE && MENU_RX_TONE == (int)RADIO_FIELD_RX_TONE,
               "RX items pass straight through as RadioParameterField");
static uint8_t menuIndex;
static NumberInput txInput; // TX Offset / TX Freq edited in place.

static void setTx(TxConfig tx) { (void)RADIO_SetTx(&tx); }

// Starts editing the offset (+/-) or the split frequency, in MHz.
static void beginTxInput(void) {
  const RadioSlot *slot = RADIO_Selected();
  const TxConfig *tx = &slot->tx;
  if (slot->source == RADIO_SOURCE_CHANNEL) { RADIO_Notify("VFO only"); return; }
  if (tx->shift == TX_SHIFT_OFF) { RADIO_Notify("TX Shift is Off"); return; }
  bool split = tx->shift == TX_SHIFT_SPLIT;
  uint32_t initial = !split ? tx->offset_hz : tx->split_hz ? tx->split_hz : slot->current.frequency_hz;
  NUMBER_Begin(&txInput, 0, split ? 1300000000u : TX_OFFSET_MAX_HZ, 1000000u, initial, true);
}

// Zero clears the value; an invalid one keeps the draft for correction.
static void txInputKey(KEY_Code_t key, Key_State_t state) {
  uint32_t value;
  if (NUMBER_Key(&txInput, key, state, &value) != NUMBER_ACCEPTED) return;
  TxConfig tx = RADIO_Selected()->tx;
  if (tx.shift == TX_SHIFT_SPLIT) tx.split_hz = value; else tx.offset_hz = value;
  if (RADIO_SetTx(&tx)) RADIO_Notify("Saved");
  else { txInput.active = true; txInput.invalid = true; }
}

static void menuChangeValue(bool up) {
  TxConfig tx = RADIO_Selected()->tx;
  switch (menuIndex) {
  case MENU_TX_ENABLE: tx.enabled = !tx.enabled; setTx(tx); return;
  case MENU_TX_POWER: (void)RADIO_CycleTxPower(up); return;
  case MENU_TX_SHIFT:
    tx.shift = (TxShift)((tx.shift + (up ? 1u : TX_SHIFT_COUNT-1u)) % TX_SHIFT_COUNT);
    setTx(tx); return;
  case MENU_TX_OFFSET: beginTxInput(); return;
  case MENU_TX_TONE: tx.tone = TONE_Next(tx.tone, up); setTx(tx); return;
#if UVK6_ENABLE_SPECTRUM
  case MENU_SPECTRUM: SCREEN_Pop(); SCREEN_Push(&spectrumScreen); return;
#endif
  case MENU_RESET: RADIO_ResetToPreset(); return;
  case MENU_SAVE_CHANNEL: SAVE_CHANNEL_Open(); return;
  default: RADIO_AdjustRx((RadioParameterField)menuIndex, up); return;
  }
}

static const char *menuValue(uint8_t item, char out[24]) {
  const RadioSlot *slot = RADIO_Selected();
  const RadioParameters *rx = &slot->current.parameters;
  const TxConfig *tx = &slot->tx;
  uint32_t hz;
  switch (item) {
  case MENU_RX_MODE: return rxModeNames[rx->mode];
  case MENU_RX_STEP: hz = rx->step_hz; break;
  case MENU_RX_BANDWIDTH:
    if (rx->mode == RX_WFM) return "Fixed";
    hz = rx->bandwidth_hz; break;
  case MENU_RX_SQUELCH: {
    Text t = TEXT_Begin(out, 24);
    TEXT_Number(&t, rx->squelch, 1, 10);
    return out;
  }
  case MENU_RX_TONE:
    if (rx->mode != RX_NFM) return "NFM only";
    TONE_Format(rx->rx_tone, out); return out;
  case MENU_TX_ENABLE: return tx->enabled ? "On" : "Off";
  case MENU_TX_POWER: return txPowerNames[tx->power <= TX_POWER_HIGH ? tx->power : 0];
  case MENU_TX_SHIFT: {
    static const char *const shifts[TX_SHIFT_COUNT] = {"Off", "+", "-", "Split"};
    return shifts[tx->shift < TX_SHIFT_COUNT ? tx->shift : 0];
  }
  case MENU_TX_OFFSET:
    if (txInput.active) { NUMBER_Text(&txInput, out); return out; }
    if (tx->shift == TX_SHIFT_OFF) return "--";
    if (tx->shift == TX_SHIFT_SPLIT && !tx->split_hz) return "Same";
    hz = tx->shift == TX_SHIFT_SPLIT ? tx->split_hz : tx->offset_hz; break;
  case MENU_TX_TONE: TONE_Format(tx->tone, out); return out;
  default: return "";
  }
  UI_FrequencyText(hz, out);
  return out;
}

static void menuRender(void) {
  static const char *const names[] = {
    "RX Mode", "RX Step", "RX Bandwidth", "RX Squelch", "RX Tone",
    "TX Enable", "TX Power", "TX Shift", "TX Offset", "TX Tone",
#if UVK6_ENABLE_SPECTRUM
    "Spectrum",
#endif
    "Reset to defaults", "Save as channel"
  };
  _Static_assert(sizeof(names)/sizeof(names[0]) == MENU_ITEM_COUNT,
                 "Every slot menu item needs a label");
  // A notice (refusal, saved, ...) briefly takes the title.
  const char *notice = RADIO_Notice();
  char title[8] = "VFO ";
  title[4] = (char)('A' + RADIO_SelectedId());
  STATUSLINE_SetText(notice ? notice : title);
  uint8_t first = menuIndex < 5 ? 0 : menuIndex-4;
  for (uint8_t i = first; i < MENU_ITEM_COUNT && i < first+5; ++i) {
    uint8_t y = 17+(i-first)*11;
    bool split = i == MENU_TX_OFFSET && RADIO_Selected()->tx.shift == TX_SHIFT_SPLIT;
    PrintMediumEx(2, y, POS_L, C_FILL, split ? "TX Freq" : names[i]);
    char text[24];
    PrintSmallEx(124, y, POS_R, C_FILL, menuValue(i, text));
    if (i == menuIndex) FillRect(0, y-7, 128, 10, C_INVERT);
  }
}

static bool menuKey(KEY_Code_t key, Key_State_t state) {
  if (key == KEY_MENU && state == KEY_LONG_PRESSED) {
    SCREEN_Pop();
    return false; // The system opens the global settings.
  }
  if (txInput.active) { txInputKey(key, state); return true; }
  if (state != KEY_RELEASED) return true;
  if (key == KEY_EXIT) SCREEN_Pop();
  else if (key == KEY_UP || key == KEY_DOWN)
    menuIndex = key == KEY_DOWN ? (menuIndex+1u)%MENU_ITEM_COUNT
                                : (menuIndex+MENU_ITEM_COUNT-1u)%MENU_ITEM_COUNT;
  else if (key == KEY_STAR || key == KEY_F || key == KEY_MENU)
    menuChangeValue(key != KEY_F);
  return true;
}

static void menuLeave(void) { txInput.active = false; }

const Screen slotMenuScreen = { .leave = menuLeave, .render = menuRender, .key = menuKey };
