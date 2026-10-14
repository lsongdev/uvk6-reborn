// Per-slot menu: RX parameters, TX settings, reset and Save as channel.
#include "apps/radio-internal.h"

static uint8_t menuIndex;

static void storeTx(TxConfig tx) {
  activeSlot.tx = gDeviceConfig.radio.slots[selectedSlot].tx = tx;
  DEVICE_RequestSave();
}

static void changeValue(bool up) {
  if (menuIndex == MENU_SAVE_CHANNEL) {
    const RadioSlot *stored = &gDeviceConfig.radio.slots[selectedSlot];
    UserChannel channel = {
      .frequency_hz = activeSlot.current.frequency_hz,
      .rx = activeSlot.current.parameters,
      .tx = activeSlot.tx,
    };
    // A broadcast's RDS station name (PS) is the default name to edit.
    const SlotLabel *label = &labels[selectedSlot];
    if (label->frequency_hz == channel.frequency_hz && label->ps[0]) {
      Text t = TEXT_Begin(channel.name, sizeof(channel.name));
      TEXT_Add(&t, label->ps);
      for (size_t n = strlen(channel.name); n && channel.name[n-1] == ' '; --n)
        channel.name[n-1] = '\0'; // PS is space-padded to 8 characters.
    }
    CHANNEL_MANAGER_StartSave(selectedSlot,
        stored->source == RADIO_SOURCE_CHANNEL ? stored->channel_number : 0, &channel);
    return;
  }
  if (gDeviceConfig.radio.slots[selectedSlot].source == RADIO_SOURCE_CHANNEL) {
    notify("VFO only");
    return;
  }
  TxConfig tx = activeSlot.tx;
  switch (menuIndex) {
  case MENU_RX_MODE: nextMode(); return;
  case MENU_RX_TONE:
    if (activeSlot.current.parameters.mode != RX_NFM) { notify("NFM only"); return; }
    break;
  case MENU_TX_ENABLE: tx.enabled = !tx.enabled; storeTx(tx); return;
  case MENU_TX_POWER: changeTxPower(up); return;
  case MENU_TX_SHIFT:
    tx.shift = (TxShift)((tx.shift + (up ? 1u : TX_SHIFT_COUNT-1u)) % TX_SHIFT_COUNT);
    storeTx(tx); return;
  case MENU_TX_OFFSET:
    if (tx.shift == TX_SHIFT_OFF) notify("TX Shift is Off");
    else beginFrequencyInput(true);
    return;
  case MENU_TX_TONE: tx.tone = TONE_Next(tx.tone, up); storeTx(tx); return;
  case MENU_RESET: // Back to the band preset; frequency and TX stay.
    request(activeSlot.current.frequency_hz, gDeviceConfig.radio.presets[activeSlot.active_band]);
    notify("Defaults restored");
    return;
  }
  RadioParameters candidate = activeSlot.current.parameters;
  if (RADIO_MODEL_Adjust(activeSlot.current.frequency_hz, &candidate,
                         RECEIVER_Capabilities(), (RadioParameterField)menuIndex, up))
    request(activeSlot.current.frequency_hz, candidate);
  else if (menuIndex == MENU_RX_BANDWIDTH && candidate.mode == RX_WFM)
    notify("Fixed");
}

static const char *menuValue(uint8_t item, char out[24]) {
  const RadioParameters *rx = &activeSlot.current.parameters;
  const TxConfig *tx = &activeSlot.tx;
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
    TONE_Format(rx->rx_tone,out); return out;
  case MENU_TX_ENABLE: return tx->enabled ? "On" : "Off";
  case MENU_TX_POWER: return txPowerNames[tx->power <= TX_POWER_HIGH ? tx->power : 0];
  case MENU_TX_SHIFT: {
    static const char *const shifts[TX_SHIFT_COUNT] = {"Off", "+", "-", "Split"};
    return shifts[tx->shift < TX_SHIFT_COUNT ? tx->shift : 0];
  }
  case MENU_TX_OFFSET:
    if (number.active && inputTx) { NUMBER_Text(&number,out); return out; }
    if (tx->shift == TX_SHIFT_OFF) return "--";
    if (tx->shift == TX_SHIFT_SPLIT && !tx->split_hz) return "Same";
    hz = tx->shift == TX_SHIFT_SPLIT ? tx->split_hz : tx->offset_hz; break;
  case MENU_TX_TONE: TONE_Format(tx->tone,out); return out;
  default: return "";
  }
  UI_FrequencyText(hz, out);
  return out;
}

void renderMenu(void) {
  static const char *const names[] = {
    "RX Mode", "RX Step", "RX Bandwidth", "RX Squelch", "RX Tone",
    "TX Enable", "TX Power", "TX Shift", "TX Offset", "TX Tone",
    "Reset to defaults", "Save as channel"
  };
  _Static_assert(sizeof(names)/sizeof(names[0]) == MENU_ITEM_COUNT,
                 "Every slot menu item needs a label");
  uint8_t first = menuIndex < 5 ? 0 : menuIndex-4;
  for (uint8_t i = first; i < MENU_ITEM_COUNT && i < first+5; ++i) {
    uint8_t y = 17+(i-first)*11;
    bool split = i == MENU_TX_OFFSET && activeSlot.tx.shift == TX_SHIFT_SPLIT;
    PrintMediumEx(2, y, POS_L, C_FILL, split ? "TX Freq" : names[i]);
    char value[24];
    PrintSmallEx(124, y, POS_R, C_FILL, menuValue(i, value));
    if (i == menuIndex)
      FillRect(0, y-7, 128, 10, C_INVERT);
  }
}

bool menuKey(KEY_Code_t key, Key_State_t state) {
  if (key == KEY_MENU && state == KEY_LONG_PRESSED) {
    inMenu = false;
    return false; // System opens global settings.
  }
  if (state == KEY_RELEASED) {
    if (key == KEY_EXIT)
      inMenu = false;
    else if (key == KEY_UP || key == KEY_DOWN)
      menuIndex = key == KEY_DOWN ? (menuIndex+1u)%MENU_ITEM_COUNT
                                 : (menuIndex+MENU_ITEM_COUNT-1u)%MENU_ITEM_COUNT;
    else if (key == KEY_STAR || key == KEY_F || key == KEY_MENU)
      changeValue(key != KEY_F);
  }
  return true;
}
