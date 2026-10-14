#include "apps/channel-manager.h"
#include "apps/apps.h"
#include "apps/textinput.h"
#include "core/text.h"
#include "hal/display.h"
#include "core/channel-store.h"
#include "ui/graphics.h"
#include "ui/statusline.h"
#include "core/radio-model.h"
#include "ui/components/frequency.h"
#include <stdio.h>
#include <string.h>

static char names[RADIO_SLOT_CAPACITY][USER_CHANNEL_NAME_BYTES];
static uint8_t nameSlot, saveSlot;
static bool active, overwriteConfirm;
static uint16_t target;
static UserChannel draft;
static ChannelManagerNotify notify;
static ChannelManagerSaved saved;

static void nameAccepted(void) {
  names[nameSlot][USER_CHANNEL_NAME_MAX] = '\0';
  gTextInputSize = 15;
  if (notify) notify("Name set");
}

void CHANNEL_MANAGER_Init(ChannelManagerNotify onNotify,
                          ChannelManagerSaved onSaved) {
  notify = onNotify;
  saved = onSaved;
}

void CHANNEL_MANAGER_LoadName(uint8_t slot, uint16_t number) {
  if (slot >= RADIO_SLOT_CAPACITY) return;
  names[slot][0] = '\0';
  UserChannel channel;
  if (number && CHANNEL_STORE_Load(number, &channel))
    memcpy(names[slot], channel.name, sizeof(names[slot]));
}

const char *CHANNEL_MANAGER_Name(uint8_t slot) {
  return slot < RADIO_SLOT_CAPACITY ? names[slot] : "";
}

void CHANNEL_MANAGER_StartName(uint8_t slot, uint16_t current_number) {
  if (slot >= RADIO_SLOT_CAPACITY) return;
  if (!names[slot][0] && current_number)
    CHANNEL_MANAGER_LoadName(slot, current_number);
  nameSlot = slot;
  gTextinputText = names[slot];
  gTextInputSize = USER_CHANNEL_NAME_MAX;
  gTextInputCallback = nameAccepted;
  APPS_run(APP_TEXTINPUT);
}

void CHANNEL_MANAGER_StartSave(uint8_t slot, uint16_t current_number,
                               const UserChannel *channel) {
  if (slot >= RADIO_SLOT_CAPACITY || !channel) return;
  if (!CHANNEL_STORE_Available()) {
    if (notify) notify("No channel memory");
    return;
  }
  saveSlot = slot;
  draft = *channel;
  if (names[slot][0])
    memcpy(draft.name, names[slot], sizeof(draft.name));
  else
    draft.name[0] = '\0';
  target = CHANNEL_STORE_IsOccupied(current_number)
      ? current_number : CHANNEL_STORE_FirstEmpty();
  if (!target) {
    for (uint16_t n = 1; n <= USER_CHANNEL_COUNT; ++n)
      if (CHANNEL_STORE_IsOccupied(n)) { target = n; break; }
  }
  if (!target) {
    if (notify) notify("Channels full");
    return;
  }
  active = true;
  overwriteConfirm = false;
  gRedrawScreen = true;
}

bool CHANNEL_MANAGER_Active(void) { return active; }

static void save(void) {
  if (!draft.name[0]) {
    Text t = TEXT_Begin(draft.name, sizeof(draft.name));
    TEXT_Add(&t, "CH ");
    TEXT_Number(&t, target, 3, 10);
  }
  if (!CHANNEL_STORE_Save(target, &draft)) {
    if (notify) notify("Save failed");
    return;
  }
  memcpy(names[saveSlot], draft.name, sizeof(names[saveSlot]));
  active = false;
  overwriteConfirm = false;
  if (saved) saved(saveSlot, target, &draft);
  if (notify) notify("Saved");
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
  char line[24];
  Text t = TEXT_Begin(line, sizeof(line));
  STATUSLINE_SetText(overwriteConfirm ? "Overwrite CH" : "Save channel");
  TEXT_Add(&t, overwriteConfirm ? "Overwrite CH " : "Save to CH ");
  TEXT_Number(&t, target, 3, 10);
  TEXT_Char(&t, '?');
  PrintMediumEx(2, 18, POS_L, C_FILL, line);
  PrintMediumEx(2, 31, POS_L, C_FILL,
                occupied ? (old.name[0] ? old.name : "(unnamed)")
                         : (draft.name[0] ? draft.name : "New channel"));
  t = TEXT_Begin(line, sizeof(line));
  TEXT_Add(&t, "RX ");
  UI_FrequencyText(draft.frequency_hz, t.at);
  PrintSmallEx(126, 42, POS_R, C_FILL, line);
  PrintSmallEx(2, 57, POS_L, C_FILL,
               overwriteConfirm ? "MENU:OK  EXIT:Back"
                                : "UP/DN:CH  MENU:OK");
  return true;
}
