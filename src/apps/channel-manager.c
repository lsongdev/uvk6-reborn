#include "apps/channel-manager.h"
#include "apps/apps.h"
#include "apps/textinput.h"
#include "hal/display.h"
#include "core/channel-store.h"
#include "ui/graphics.h"
#include "ui/statusline.h"
#include "core/radio-model.h"
#include "ui/components/frequency.h"
#include <stdio.h>
#include <string.h>

static char names[RADIO_SLOT_CAPACITY][USER_CHANNEL_NAME_BYTES];
static uint8_t nameVfo, saveVfo;
static bool active, overwriteConfirm;
static uint16_t target;
static UserChannel draft;
static ChannelManagerNotify notify;
static ChannelManagerSaved saved;

static void nameAccepted(void) {
  names[nameVfo][USER_CHANNEL_NAME_MAX] = '\0';
  gTextInputSize = 15;
  if (notify) notify("Channel name ready");
}

void CHANNEL_MANAGER_Init(ChannelManagerNotify onNotify,
                          ChannelManagerSaved onSaved) {
  notify = onNotify;
  saved = onSaved;
}

void CHANNEL_MANAGER_LoadName(uint8_t vfo, uint16_t number) {
  if (vfo >= RADIO_SLOT_CAPACITY) return;
  names[vfo][0] = '\0';
  UserChannel channel;
  if (number && CHANNEL_STORE_Load(number, &channel))
    memcpy(names[vfo], channel.name, sizeof(names[vfo]));
}

const char *CHANNEL_MANAGER_Name(uint8_t vfo) {
  return vfo < RADIO_SLOT_CAPACITY ? names[vfo] : "";
}

void CHANNEL_MANAGER_StartName(uint8_t vfo, uint16_t current_number) {
  if (vfo >= RADIO_SLOT_CAPACITY) return;
  if (!names[vfo][0] && current_number)
    CHANNEL_MANAGER_LoadName(vfo, current_number);
  nameVfo = vfo;
  gTextinputText = names[vfo];
  gTextInputSize = USER_CHANNEL_NAME_MAX;
  gTextInputCallback = nameAccepted;
  APPS_run(APP_TEXTINPUT);
}

void CHANNEL_MANAGER_StartSave(uint8_t vfo, uint16_t current_number,
                               const UserChannel *channel) {
  if (vfo >= RADIO_SLOT_CAPACITY || !channel) return;
  if (!CHANNEL_STORE_Available()) {
    if (notify) notify("Channel store unavailable");
    return;
  }
  saveVfo = vfo;
  draft = *channel;
  if (names[vfo][0])
    memcpy(draft.name, names[vfo], sizeof(draft.name));
  else
    draft.name[0] = '\0';
  target = CHANNEL_STORE_IsOccupied(current_number)
      ? current_number : CHANNEL_STORE_FirstEmpty();
  if (!target) {
    for (uint16_t n = 1; n <= USER_CHANNEL_COUNT; ++n)
      if (CHANNEL_STORE_IsOccupied(n)) { target = n; break; }
  }
  if (!target) {
    if (notify) notify("Channel memory full");
    return;
  }
  active = true;
  overwriteConfirm = false;
  gRedrawScreen = true;
}

bool CHANNEL_MANAGER_Active(void) { return active; }

static void save(void) {
  if (!draft.name[0])
    snprintf(draft.name, sizeof(draft.name), "CH %03u", target);
  if (!CHANNEL_STORE_Save(target, &draft)) {
    if (notify) notify("Channel save failed");
    return;
  }
  memcpy(names[saveVfo], draft.name, sizeof(names[saveVfo]));
  active = false;
  overwriteConfirm = false;
  if (saved) saved(saveVfo, target, &draft);
  if (notify) notify("Channel saved");
}

bool CHANNEL_MANAGER_HandleKey(KEY_Code_t key, Key_State_t state) {
  if (!active) return false;
  if (state == KEY_RELEASED) {
    if (key == KEY_EXIT) {
      if (overwriteConfirm) overwriteConfirm = false;
      else active = false;
    } else if (key == KEY_UP || key == KEY_DOWN) {
      target = key == KEY_UP ? (target % USER_CHANNEL_COUNT) + 1u
          : (target == 1 ? USER_CHANNEL_COUNT : target - 1u);
      overwriteConfirm = false;
    } else if (key == KEY_MENU) {
      if (CHANNEL_STORE_IsOccupied(target) && !overwriteConfirm)
        overwriteConfirm = true;
      else
        save();
    }
    gRedrawScreen = true;
  }
  return true;
}

bool CHANNEL_MANAGER_Render(void) {
  if (!active) return false;
  UserChannel old;
  bool occupied = CHANNEL_STORE_Load(target, &old);
  char frequency[16];
  STATUSLINE_SetText(overwriteConfirm ? "Overwrite CH" : "Save channel");
  PrintMediumEx(2, 18, POS_L, C_FILL, "%s CH %03u?",
                overwriteConfirm ? "Overwrite" : "Save to", target);
  PrintMediumEx(2, 31, POS_L, C_FILL, "%s",
                occupied ? (old.name[0] ? old.name : "(unnamed)")
                         : (draft.name[0] ? draft.name : "New channel"));
  const char *unit = UI_FormatFrequency(draft.frequency_hz, frequency);
  PrintSmallEx(126, 42, POS_R, C_FILL, "RX %s %s", frequency, unit);
  PrintSmallEx(2, 57, POS_L, C_FILL,
               overwriteConfirm ? "MENU: overwrite  EXIT: back"
                                : "UP/DOWN: slot  MENU: select");
  return true;
}
