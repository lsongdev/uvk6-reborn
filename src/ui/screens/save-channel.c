#include "ui/screens/save-channel.h"
#include "ui/screens/text-input.h"
#include "radio/radio.h"
#include "devices/display.h"
#include "store/channel-store.h"
#include "core/text.h"
#include "ui/frequency.h"
#include "ui/graphics.h"
#include "ui/screens/statusline.h"
#include <string.h>

static bool overwriteConfirm;
static uint16_t target;
static UserChannel draft;
static char nameDraft[USER_CHANNEL_NAME_BYTES];

void SAVE_CHANNEL_Open(void) {
  if (!CHANNEL_STORE_Available()) { RADIO_Notify("No channel memory"); return; }
  RADIO_ChannelDraft(&draft);
  uint16_t current = RADIO_ChannelNumber();
  target = CHANNEL_STORE_IsOccupied(current) ? current : CHANNEL_STORE_FirstEmpty();
  if (!target) { RADIO_Notify("Channels full"); return; }
  overwriteConfirm = false;
  SCREEN_Push(&saveChannelScreen);
}

// The frequency, e.g. "438.500MHz": the channel number is already shown
// wherever the name is, so the frequency is the more useful default.
static void defaultName(char name[USER_CHANNEL_NAME_BYTES]) {
  char text[20];
  const char *unit = UI_FormatFrequency(draft.frequency_hz, text);
  Text t = TEXT_Begin(name, USER_CHANNEL_NAME_BYTES);
  TEXT_Add(&t, text);
  TEXT_Add(&t, unit);
}

// Text input accepted the name: write the channel. An empty name becomes
// the default; leaving text input without accepting saves nothing.
static void channelSave(void) {
  memcpy(draft.name, nameDraft, sizeof(draft.name));
  draft.name[USER_CHANNEL_NAME_MAX] = '\0';
  if (!draft.name[0]) defaultName(draft.name);
  if (!CHANNEL_STORE_Save(target, &draft)) { RADIO_Notify("Save failed"); return; }
  RADIO_ChannelSaved(target, &draft);
  RADIO_Notify("Saved");
}

// Overwriting offers the old name; a new channel offers the draft's name
// (e.g. the RDS station name), else the frequency. Text input replaces this
// screen, so finishing it returns to where Save as channel was chosen.
static void askName(void) {
  UserChannel old;
  memcpy(nameDraft, draft.name, sizeof(nameDraft));
  if (CHANNEL_STORE_Load(target, &old)) memcpy(nameDraft, old.name, sizeof(nameDraft));
  if (!nameDraft[0]) defaultName(nameDraft);
  TEXT_INPUT_Open(nameDraft, USER_CHANNEL_NAME_MAX, channelSave, true);
}

static bool channelKey(KEY_Code_t key, Key_State_t state) {
  if (state != KEY_RELEASED) return true;
  if (key == KEY_EXIT) {
    if (overwriteConfirm) overwriteConfirm = false;
    else SCREEN_Pop();
  } else if (key == KEY_UP || key == KEY_DOWN) {
    target = key == KEY_UP ? (target % USER_CHANNEL_COUNT) + 1u
        : (target == 1 ? USER_CHANNEL_COUNT : target - 1u);
    overwriteConfirm = false;
  } else if (key == KEY_MENU) {
    if (CHANNEL_STORE_IsOccupied(target) && !overwriteConfirm) overwriteConfirm = true;
    else askName();
  }
  gRedrawScreen = true;
  return true;
}

static void channelRender(void) {
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
               overwriteConfirm ? "MENU:OK  EXIT:Back" : "UP/DN:CH  MENU:OK");
}

const Screen saveChannelScreen = { .render = channelRender, .key = channelKey };
