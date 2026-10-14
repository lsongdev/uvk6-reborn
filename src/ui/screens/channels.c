#include "ui/screens/channels.h"
#include "ui/screens/text-input.h"
#include "radio/radio.h"
#include "core/channel-store.h"
#include "core/text.h"
#include "ui/frequency.h"
#include "ui/graphics.h"
#include "ui/screens/statusline.h"
#include <string.h>

#define ROWS 4
static uint16_t cursor, top; // Channel numbers; zero when the list is empty.
static bool confirmDelete;
static const char *message;
static char nameDraft[USER_CHANNEL_NAME_BYTES];

// Next occupied number after n in one direction, without wrapping; 0 if none.
static uint16_t step(uint16_t n, bool up) {
  for (n = up ? n + 1u : n - 1u; n >= 1 && n <= USER_CHANNEL_COUNT; n = up ? n + 1u : n - 1u)
    if (CHANNEL_STORE_IsOccupied(n)) return n;
  return 0;
}
static uint16_t first(void) { return step(0, true); }
// Rows from top down to cursor, so the cursor can be kept on screen.
static uint8_t rowOf(uint16_t n) {
  uint8_t row = 0;
  for (uint16_t at = top; at && at != n; at = step(at, true)) ++row;
  return row;
}
static void move(bool down) {
  uint16_t next = step(cursor, down);
  if (!next) return;
  cursor = next;
  if (!down && cursor < top) top = cursor;
  while (rowOf(cursor) >= ROWS) top = step(top, true);
}

static void enter(void) {
  if (!CHANNEL_STORE_IsOccupied(cursor)) cursor = top = first();
  if (!CHANNEL_STORE_IsOccupied(top) || top > cursor) top = cursor;
  confirmDelete = false;
  message = NULL;
}

static void renamed(void) {
  UserChannel channel;
  if (!CHANNEL_STORE_Load(cursor, &channel)) return;
  memcpy(channel.name, nameDraft, sizeof(channel.name));
  channel.name[USER_CHANNEL_NAME_MAX] = '\0';
  message = CHANNEL_STORE_Save(cursor, &channel) ? "Saved" : "Save failed";
  RADIO_ChannelsChanged();
}

static bool key(KEY_Code_t key, Key_State_t state) {
  if (state != KEY_RELEASED && state != KEY_LONG_PRESSED_CONT) return true;
  message = NULL;
  if (confirmDelete) {
    if (state == KEY_RELEASED && key == KEY_MENU) {
      uint16_t gone = cursor;
      uint16_t next = step(gone, true) ? step(gone, true) : step(gone, false);
      if (CHANNEL_STORE_Delete(gone)) {
        RADIO_ChannelsChanged();
        if (top == gone) top = next;
        cursor = next;
        if (top > cursor) top = cursor;
        message = "Deleted";
      } else message = "Save failed";
    }
    if (state == KEY_RELEASED) confirmDelete = false;
    return true;
  }
  if (key == KEY_UP || key == KEY_DOWN) { move(key == KEY_DOWN); return true; }
  if (state != KEY_RELEASED) return true;
  if (key == KEY_EXIT) { SCREEN_Pop(); return true; }
  if (!cursor) return true;
  if (key == KEY_STAR) confirmDelete = true;
  else if (key == KEY_MENU) {
    UserChannel channel;
    if (!CHANNEL_STORE_Load(cursor, &channel)) return true;
    memcpy(nameDraft, channel.name, sizeof(nameDraft));
    TEXT_INPUT_Open(nameDraft, USER_CHANNEL_NAME_MAX, renamed, false);
  }
  return true;
}

static void render(void) {
  char line[20];
  if (confirmDelete) {
    Text t = TEXT_Begin(line, sizeof(line));
    TEXT_Add(&t, "Delete CH ");
    TEXT_Number(&t, cursor, 3, 10);
    TEXT_Char(&t, '?');
    PrintMedium(2, 24, line);
    PrintSmall(2, 57, "MENU:OK  EXIT:Back");
    return;
  }
  if (!cursor) { PrintMedium(2, 24, "No channels"); return; }
  uint16_t n = top;
  for (uint8_t row = 0; row < ROWS && n; ++row, n = step(n, true)) {
    UserChannel channel;
    uint8_t y = 17 + row * 11;
    Text t = TEXT_Begin(line, sizeof(line));
    TEXT_Number(&t, n, 3, 10);
    PrintSmall(2, y, line);
    if (CHANNEL_STORE_Load(n, &channel)) {
      // The frequency keeps its full width; the name gets what is left.
      UI_FrequencyText(channel.frequency_hz, line);
      uint8_t left = 124 - UI_SmallTextWidth(line);
      PrintSmallEx(124, y, POS_R, C_FILL, line);
      UI_MediumTextWindow(17, y, left - 3 - 17, channel.name, 0);
    }
    if (n == cursor) FillRect(0, y - 7, 128, 10, C_INVERT);
  }
  PrintSmall(2, 61, message ? message : "MENU:Name  *:Delete");
}

const Screen channelsScreen = { .title = "Channels", .enter = enter, .render = render, .key = key };
