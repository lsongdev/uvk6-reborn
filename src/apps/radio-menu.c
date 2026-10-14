// Per-slot menu: RX parameters, watch, TX settings and channel actions.
#include "apps/radio-internal.h"

static uint8_t menuIndex;

static void changeValue(bool up) {
  if (menuIndex == MENU_CHANNEL_NAME) {
    CHANNEL_MANAGER_StartName(selectedSlot,
        gDeviceConfig.radio.slots[selectedSlot].channel_number);
    return;
  }
  if (menuIndex == MENU_CHANNEL_SAVE) {
    UserChannel channel = {
      .frequency_hz = activeSlot.current.frequency_hz,
      .rx = activeSlot.current.parameters,
      .tx = activeSlot.tx,
    };
    CHANNEL_MANAGER_StartSave(selectedSlot,
        gDeviceConfig.radio.slots[selectedSlot].source == RADIO_SOURCE_CHANNEL
            ? gDeviceConfig.radio.slots[selectedSlot].channel_number : 0,
        &channel);
    return;
  }
  if (gDeviceConfig.radio.slots[selectedSlot].source == RADIO_SOURCE_CHANNEL) {
    notify("VFO only");
    return;
  }
  if (menuIndex == MENU_RX_MODE) {
    nextMode();
    return;
  }
  if (menuIndex == MENU_TX_FREQUENCY) {
    if (!up) {
      gDeviceConfig.radio.slots[selectedSlot].tx.frequency_hz = 0;
      activeSlot.tx.frequency_hz = 0;
      DEVICE_RequestSave();
    } else {
      beginFrequencyInput(true);
    }
    return;
  }
  if (menuIndex == MENU_TX_TONE) {
    gDeviceConfig.radio.slots[selectedSlot].tx.tone = TONE_Next(gDeviceConfig.radio.slots[selectedSlot].tx.tone, up);
    activeSlot.tx.tone = gDeviceConfig.radio.slots[selectedSlot].tx.tone;
    DEVICE_RequestSave();
    return;
  }
  if (menuIndex == MENU_RX_TONE && activeSlot.current.parameters.mode != RX_NFM) {
    notify("NFM only");
    return;
  }
  if (menuIndex == MENU_TX_ENABLE) {
    gDeviceConfig.radio.slots[selectedSlot].tx.enabled = !gDeviceConfig.radio.slots[selectedSlot].tx.enabled;
    activeSlot.tx.enabled = gDeviceConfig.radio.slots[selectedSlot].tx.enabled;
    DEVICE_RequestSave();
    return;
  }
  if (menuIndex == MENU_TX_POWER) {
    changeTxPower(up);
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
  case MENU_TX_ENABLE: return activeSlot.tx.enabled ? "On" : "Off";
  case MENU_TX_POWER: {
    return txPowerNames[activeSlot.tx.power <= TX_POWER_HIGH ? activeSlot.tx.power : 0];
  }
  case MENU_TX_FREQUENCY:
    if (number.active && inputTx) { NUMBER_Text(&number,out); return out; }
    if (!activeSlot.tx.frequency_hz) return "Same";
    hz = activeSlot.tx.frequency_hz; break;
  case MENU_TX_TONE: TONE_Format(activeSlot.tx.tone,out); return out;
  case MENU_CHANNEL_NAME:
    return CHANNEL_MANAGER_Name(selectedSlot)[0] ? CHANNEL_MANAGER_Name(selectedSlot) : "(none)";
  case MENU_CHANNEL_SAVE: return "Choose CH";
  default: return "";
  }
  UI_FrequencyText(hz, out);
  return out;
}

void renderMenu(void) {
  static const char *const names[] = {
    "RX Mode", "RX Step", "RX Bandwidth", "RX Squelch", "RX Tone", "TX Enable", "TX Power", "TX Frequency", "TX Tone", "Name", "Save"
  };
  _Static_assert(sizeof(names)/sizeof(names[0]) == MENU_ITEM_COUNT,
                 "Every VFO menu item needs a label");
  uint8_t first = menuIndex < 5 ? 0 : menuIndex-4;
  for (uint8_t i = first; i < MENU_ITEM_COUNT && i < first+5; ++i) {
    uint8_t y = 17+(i-first)*11;
    if (i == MENU_TX_FREQUENCY && number.active) PrintSmallEx(2, y, POS_L, C_FILL, "TX Freq");
    else PrintMediumEx(2, y, POS_L, C_FILL, names[i]);
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
