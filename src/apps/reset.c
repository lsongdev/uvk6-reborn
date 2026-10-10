#include "reset.h"
#include "../driver/eeprom.h"
#include "../driver/st7565.h"
#include "../external/CMSIS_5/Device/ARM/ARMCM0/Include/ARMCM0.h"
#include "../helper/channels.h"
#include "../radio.h"
#include "../settings.h"
#include "../ui/graphics.h"
#include "../ui/statusline.h"

typedef enum {
  RESET_FULL,
  RESET_UNKNOWN,
} ResetType;

static struct {
  uint32_t totalBytes;
  uint32_t doneBytes;
  uint16_t pageSize;
  uint16_t maxChannels;
  uint16_t currentItem;
  ResetType type;
  bool confirm;
} resetState;

static VFO defaultVfos[4] = {
    {.rxF = 14550000, .meta.type = TYPE_VFO, .gainIndex = AUTO_GAIN_INDEX, .radio = RADIO_BK4819},
    {.rxF = 43312500, .meta.type = TYPE_VFO, .gainIndex = AUTO_GAIN_INDEX, .radio = RADIO_BK4819},
    {.rxF = 25355000, .meta.type = TYPE_VFO, .gainIndex = AUTO_GAIN_INDEX, .radio = RADIO_BK4819},
    {.rxF = 40065000, .meta.type = TYPE_VFO, .gainIndex = AUTO_GAIN_INDEX, .radio = RADIO_BK4819},
};

static void startReset(ResetType type) {
  resetState.type = type;
  resetState.doneBytes = 0;
  resetState.currentItem = 0;
  resetState.totalBytes = SETTINGS_SIZE + resetState.maxChannels * CH_SIZE;
}

static bool processReset(void) {
  // Initialize only this firmware's settings/channels, not the entire EEPROM.
  if (resetState.currentItem == 0) {
    SETTINGS_Save();
    resetState.doneBytes += SETTINGS_SIZE;
    resetState.currentItem++;
    return false;
  }

  uint16_t chIndex = resetState.currentItem - 1;
  uint8_t numVFOs = ARRAY_SIZE(defaultVfos);

  if (chIndex < resetState.maxChannels - numVFOs) {
    CHANNELS_Delete(chIndex);
    resetState.doneBytes += CH_SIZE;
    resetState.currentItem++;
    return false;
  }

  if (chIndex < resetState.maxChannels) {
    uint8_t vfoIndex = chIndex - (resetState.maxChannels - numVFOs);
    VFO vfo = defaultVfos[vfoIndex];
    sprintf(vfo.name, "VFO-%c", 'A' + vfoIndex);
    vfo.meta.type = TYPE_VFO;
    vfo.channel = 0;
    vfo.modulation = MOD_FM;
    vfo.bw = BK4819_FILTER_BW_12k;
    vfo.txF = 0;
    vfo.offsetDir = OFFSET_NONE;
    vfo.allowTx = false;
    vfo.code.rx.type = 0;
    vfo.code.tx.type = 0;
    vfo.meta.readonly = false;
    vfo.squelch.value = 4;
    vfo.step = STEP_25_0kHz;
    CHANNELS_Save(chIndex, &vfo);
    resetState.doneBytes += CH_SIZE;
    resetState.currentItem++;
    return false;
  }

  return true;
}

void RESET_Init(void) {
  resetState.type = RESET_UNKNOWN;
  resetState.confirm = false;
  gSettings.keylock = false;
  gSettings.eepromType = EEPROM_DetectType();
  resetState.pageSize = SETTINGS_GetPageSize();
  resetState.maxChannels = CHANNELS_GetCountMax();
  STATUSLINE_SetText("%s", gSettings.eepromType < EEPROM_UNKNOWN
                                 ? EEPROM_TYPE_NAMES[gSettings.eepromType]
                                 : "EEPROM UNKNOWN");
}

void RESET_Update(void) {
  if (resetState.type == RESET_UNKNOWN || !processReset()) {
    return;
  }
  NVIC_SystemReset();
}

void RESET_Render(void) {
  if (resetState.type == RESET_UNKNOWN) {
    if (resetState.confirm) {
      PrintMedium(2, 18, "Overwrite channels?");
      PrintMedium(2, 30, "1: YES  EXIT: NO");
    } else {
      PrintMedium(2, 18, "No valid config");
      PrintMedium(2, 30, "Back up EEPROM!");
      PrintMedium(2, 42, "0: Initialize");
    }
    return;
  }

  uint8_t progress = ConvertDomain(resetState.doneBytes, 0, resetState.totalBytes, 0, 100);
  const uint8_t TOP = 28;
  DrawRect(13, TOP, 102, 9, C_FILL);
  FillRect(14, TOP + 1, progress, 7, C_FILL);
  PrintMediumEx(LCD_XCENTER, TOP + 7, POS_C, C_INVERT, "%u%", progress);
}

bool RESET_key(KEY_Code_t k, Key_State_t state) {
  if (state != KEY_RELEASED || resetState.type != RESET_UNKNOWN)
    return false;

  if (k == KEY_EXIT) {
    resetState.confirm = false;
    return true;
  }
  if (gSettings.eepromType == EEPROM_UNKNOWN)
    return true; // Unknown physical storage cannot be initialized safely.

  if (!resetState.confirm && k == KEY_0) {
    resetState.confirm = true;
    return true;
  }
  if (resetState.confirm && k == KEY_1) {
    EEPROM_SetWritesEnabled(true); // User explicitly authorizes data overwrite.
    startReset(RESET_FULL);
    return true;
  }
  return true;
}
