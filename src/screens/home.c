// Main screen: slot cards, title, tune confirmation and the key map.
#include "screens/home.h"
#include "screens/slot-menu.h"
#include "radio/radio.h"
#include "core/device-config.h"
#include "core/text.h"
#include "core/misc.h"
#include "core/receiver.h"
#include "hal/display.h"
#include "hal/scheduler.h"
#include "ui/frequency.h"
#include "ui/radio-card.h"
#include "ui/graphics.h"
#include "ui/number-input.h"
#include "screens/statusline.h"

static NumberInput keypad;      // Frequency entry on the selected card.
static bool functionPending;    // F was pressed; the next key is a shortcut.
static uint32_t functionAt;

void HOME_CancelShortcut(void) {
  if (!functionPending) return;
  functionPending = false;
  gRedrawScreen = true;
}
static void expireShortcut(void) {
  if (functionPending && Now()-functionAt >= 3000u) HOME_CancelShortcut();
}

// --- Rendering --------------------------------------------------------------

static const UI_RadioCardActivity activities[] = {
  [RADIO_ACTIVITY_IDLE] = UI_RADIO_CARD_IDLE,
  [RADIO_ACTIVITY_RECEIVING] = UI_RADIO_CARD_RECEIVING,
  [RADIO_ACTIVITY_TRANSMITTING] = UI_RADIO_CARD_TRANSMITTING,
  [RADIO_ACTIVITY_SEEKING] = UI_RADIO_CARD_SEEKING,
  [RADIO_ACTIVITY_UNAVAILABLE] = UI_RADIO_CARD_UNAVAILABLE,
  [RADIO_ACTIVITY_FAULT] = UI_RADIO_CARD_FAULT,
};

// Maps radio state to the card's display data; header owns "CH nnn".
static void buildCard(uint8_t id, const char *draft, const RadioSlotStatus *s,
                      char header[16], UI_RadioCardView *view) {
  bool selected = id == RADIO_SelectedId();
  const RadioParameters *p = &s->slot.current.parameters;
  const char *band = radioBands[s->slot.active_band].name;
  const char *name = s->name[0] ? s->name : NULL;
  if (s->slot.source == RADIO_SOURCE_CHANNEL) {
    Text t = TEXT_Begin(header, 16);
    TEXT_Add(&t, "CH ");
    TEXT_Number(&t, s->slot.channel_number, 3, 10);
    band = s->channel_missing ? "Channel unavailable" : header;
    name = s->channel_missing ? NULL : name ? name : header;
  }
  const char *notice = RADIO_Notice();
  *view = (UI_RadioCardView){
    .id = 'A'+id, .selected = selected,
    .edit_text = selected && keypad.active ? draft : NULL,
    .frequency_hz = s->frequency_hz,
    .mode = p->mode, .band_name = band,
    .changed = s->modified, .step_hz = p->step_hz,
    .name = name, .radio_text = s->radio_text,
    .header_message = !selected ? NULL :
        keypad.active ? (keypad.invalid ? "Invalid value" : NULL) :
        notice ? notice : RADIO_WatchStatus(),
    .signal_valid = s->receiving, .signal_level = s->signal_level,
    .activity = activities[s->activity],
    .power_label = s->slot.tx.power == TX_POWER_HIGH ? "HIGH" :
                   s->slot.tx.power == TX_POWER_MEDIUM ? "MID" : "LOW",
  };
  // FC shows the latest measured carrier live where the frequency sits,
  // the detector range in the header and the scope in the mode's place.
  if (s->fc_running) {
    view->name = view->radio_text = NULL;
    view->header_message = s->capture_range == CAPTURE_RANGE_VHF ? "FC VHF" : "FC UHF";
    view->mode_text = s->capture_scope == CAPTURE_AUTO ? "Auto" :
                      s->capture_scope == CAPTURE_VHF ? "VHF" : "UHF";
    if (s->capture_hz) view->frequency_hz = s->capture_hz;
    else view->edit_text = "---";
  }
}

// MR/VFO for the selection, or with several slots the watched slot heard.
static void renderTitle(void) {
  char title[8];
  Text t = TEXT_Begin(title, sizeof(title));
  bool channel = RADIO_Selected()->source == RADIO_SOURCE_CHANNEL;
  if (RADIO_PendingTune()) TEXT_Add(&t, "VFO ");
  else if (channel) TEXT_Add(&t, "MR ");
  else if (gDeviceConfig.radio.slot_count > 1) {
    uint8_t heard = RADIO_Heard();
    TEXT_Add(&t, "DW ");
    TEXT_Char(&t, heard < RADIO_SLOT_CAPACITY ? 'A'+heard : '-');
    STATUSLINE_SetText(title);
    return;
  } else {
    STATUSLINE_SetText("RADIO");
    return;
  }
  TEXT_Char(&t, 'A'+RADIO_SelectedId());
  STATUSLINE_SetText(title);
}

static void renderConfirmation(const RxPlan *plan) {
  char line[32];
  Text t = TEXT_Begin(line, sizeof(line));
  TEXT_Add(&t, "Apply ");
  TEXT_Add(&t, rxModeNames[plan->parameters.mode]);
  TEXT_Char(&t, '?');
  PrintMedium(2, 18, line);
  UI_FrequencyText(plan->frequency_hz, line);
  PrintMedium(2, 30, line);
  t = TEXT_Begin(line, sizeof(line));
  TEXT_Add(&t, "BW ");
  TEXT_Number(&t, plan->parameters.bandwidth_hz, 1, 10);
  TEXT_Add(&t, " Hz STEP ");
  TEXT_Number(&t, plan->parameters.step_hz, 1, 10);
  TEXT_Add(&t, " Hz");
  PrintSmall(2, 40, line);
  PrintSmall(2, 57, "MENU:OK  EXIT:Back");
}

static void homeRender(void) {
  renderTitle();
  STATUSLINE_SetShortcut(functionPending);
  const RxPlan *pending = RADIO_PendingTune();
  if (pending && !RADIO_Notice()) { renderConfirmation(pending); return; }
  char draft[18];
  if (keypad.active) NUMBER_Text(&keypad, draft);
  uint8_t first = RADIO_STATE_VisibleFirst(&gDeviceConfig.radio);
  uint8_t visible = MIN(gDeviceConfig.radio.slot_count, RADIO_SLOT_VISIBLE);
  // One status per row: card text points into it until drawing ends.
  RadioSlotStatus status[RADIO_SLOT_VISIBLE];
  char header[RADIO_SLOT_VISIBLE][16];
  for (uint8_t row = 0; row < visible; ++row) {
    UI_RadioCardView view;
    RADIO_SlotStatus(first + row, &status[row]);
    buildCard(first + row, draft, &status[row], header[row], &view);
    UI_RadioCardRender(8 + row * UI_RADIO_CARD_HEIGHT, &view);
  }
}

// --- Keys -------------------------------------------------------------------

static void cycleTxPower(void) {
  static char text[20]; // Outlives the call: the notice keeps the pointer.
  if (!RADIO_CycleTxPower(true)) return;
  Text t = TEXT_Begin(text, sizeof(text));
  TEXT_Add(&t, "TX Power: ");
  TEXT_Add(&t, txPowerNames[RADIO_Selected()->tx.power]);
  RADIO_Notify(text);
}

static void shortcutKey(KEY_Code_t key) {
  HOME_CancelShortcut();
  switch (key) {
  case KEY_1: RADIO_NextBand(); break;
  case KEY_0: RADIO_NextMode(); break;
  case KEY_2: RADIO_SwitchSlot(); break;
  case KEY_3: RADIO_SwitchSource(); break;
  case KEY_4: RADIO_StartCapture(); break;
  case KEY_6: cycleTxPower(); break;
  case KEY_STAR: RADIO_StartSeek(); break;
  default:
    if (key != KEY_F && key != KEY_EXIT) RADIO_Notify("Not supported");
    break;
  }
}

static void keypadKey(KEY_Code_t key, Key_State_t state) {
  uint32_t value;
  if (NUMBER_Key(&keypad, key, state, &value) == NUMBER_ACCEPTED) RADIO_TuneTo(value);
  gRedrawScreen = true;
}

static bool homeKey(KEY_Code_t key, Key_State_t state) {
  expireShortcut();
  bool released = state == KEY_RELEASED;
  // FC and Seek own every key while they run.
  if (RADIO_Capturing()) {
    if (released && key == KEY_EXIT) RADIO_CaptureCancel();
    else if (released && key == KEY_MENU) RADIO_CaptureAdopt();
    else if (released && (key == KEY_SIDE1 || key == KEY_SIDE2)) RADIO_CaptureNextScope();
    return true;
  }
  if (keypad.active) { keypadKey(key, state); return true; }
  if (RADIO_Seeking()) {
    if (released && (key == KEY_EXIT || key == KEY_MENU)) RADIO_CancelSeek();
    return true;
  }
  if (key == KEY_PTT) {
    HOME_CancelShortcut();
    if (state == KEY_PRESSED) RADIO_PttPress();
    else if (released) RADIO_PttRelease();
    return true;
  }
  if (RADIO_TxBusy()) return true;
  if (RADIO_PendingTune()) {
    if (released && (key == KEY_MENU || key == KEY_EXIT)) RADIO_ConfirmTune(key == KEY_MENU);
    return true;
  }
  // F is a one-shot prefix: the next key is a shortcut and nothing else.
  if (functionPending) {
    if (key == KEY_MENU && state == KEY_LONG_PRESSED) {
      HOME_CancelShortcut();
      return false; // Keep the global settings shortcut available.
    }
    if (released) shortcutKey(key);
    return true;
  }
  if ((key == KEY_UP || key == KEY_DOWN) && (released || state == KEY_LONG_PRESSED_CONT)) {
    RADIO_Step(key == KEY_UP);
    return true;
  }
  if (!released) return false;
  switch (key) {
  case KEY_F: functionPending = true; functionAt = Now(); gRedrawScreen = true; break;
  case KEY_SIDE1: RADIO_NextBand(); break;
  case KEY_SIDE2: RADIO_NextMode(); break;
  case KEY_MENU: SCREEN_Push(&slotMenuScreen); break;
  case KEY_EXIT: break; // The main screen has nothing to close.
  default:
    if (key > KEY_9) return false;
    if (RADIO_Selected()->source == RADIO_SOURCE_CHANNEL) { RADIO_Notify("VFO only"); break; }
    NUMBER_Begin(&keypad, RADIO_MIN_HZ, RADIO_MAX_HZ, 1000000u, 0, false);
    uint32_t unused;
    NUMBER_Key(&keypad, key, state, &unused);
    break;
  }
  return true;
}

// Covered by another screen: drafts and key-driven features end, the radio
// keeps receiving underneath.
static void homeLeave(void) {
  keypad.active = false;
  HOME_CancelShortcut();
  STATUSLINE_SetShortcut(false);
  RADIO_CancelInteractive();
}

const Screen homeScreen = {
  .leave = homeLeave, .update = expireShortcut, .render = homeRender, .key = homeKey,
};
