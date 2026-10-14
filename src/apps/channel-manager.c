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
#include <string.h>

static uint8_t saveSlot;
static bool active, overwriteConfirm;
static uint16_t target;
static UserChannel draft;
static char nameDraft[USER_CHANNEL_NAME_BYTES];
static ChannelManagerNotify notify;
static ChannelManagerSaved saved;

void CHANNEL_MANAGER_Init(ChannelManagerNotify onNotify,
                          ChannelManagerSaved onSaved) {
  notify = onNotify;
  saved = onSaved;
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
  target = CHANNEL_STORE_IsOccupied(current_number)
      ? current_number : CHANNEL_STORE_FirstEmpty();
  if (!target) {
    if (notify) notify("Channels full");
    return;
  }
  active = true;
  overwriteConfirm = false;
  gRedrawScreen = true;
}

bool CHANNEL_MANAGER_Active(void) { return active; }

// Text input accepted the name: write the channel. An empty name becomes
// "CH nnn"; leaving text input without accepting saves nothing.
static void save(void) {
  memcpy(draft.name, nameDraft, sizeof(draft.name));
  draft.name[USER_CHANNEL_NAME_MAX] = '\0';
  if (!draft.name[0]) {
    Text t = TEXT_Begin(draft.name, sizeof(draft.name));
    TEXT_Add(&t, "CH ");
    TEXT_Number(&t, target, 3, 10);
  }
  if (!CHANNEL_STORE_Save(target, &draft)) {
    if (notify) notify("Save failed");
    return;
  }
  if (saved) saved(saveSlot, target, &draft);
  if (notify) notify("Saved");
}

// Overwriting offers the old name, a new channel starts empty.
static void askName(void) {
  UserChannel old;
  nameDraft[0] = '\0';
  if (CHANNEL_STORE_Load(target, &old)) memcpy(nameDraft, old.name, sizeof(nameDraft));
  active = false;
  overwriteConfirm = false;
  gTextinputText = nameDraft;
  gTextInputSize = USER_CHANNEL_NAME_MAX;
  gTextInputCallback = save;
  APPS_run(APP_TEXTINPUT);
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
        askName();
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
                occupied ? (old.name[0] ? old.name : "(unnamed)") : "Empty");
  t = TEXT_Begin(line, sizeof(line));
  TEXT_Add(&t, "RX ");
  UI_FrequencyText(draft.frequency_hz, t.at);
  PrintSmallEx(126, 42, POS_R, C_FILL, line);
  PrintSmallEx(2, 57, POS_L, C_FILL,
               overwriteConfirm ? "MENU:OK  EXIT:Back"
                                : "UP/DN:CH  MENU:OK");
  return true;
}
