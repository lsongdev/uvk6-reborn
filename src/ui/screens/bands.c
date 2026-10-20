#include "ui/screens/bands.h"
#include "store/device-config.h"
#include "radio/receiver.h"
#include "ui/graphics.h"
#include "core/text.h"
#include "ui/screens/statusline.h"

static uint8_t selected, field;
// Only bands this hardware can receive in are listed; row indexes `shown`.
static uint8_t shown[RADIO_STATE_BANDS], shownCount, row;
static bool editing;
static RadioParameters draft;
static const char *message;

enum { FIELD_CURRENT = 5, FIELD_DEFAULTS, FIELD_SAVE, FIELD_COUNT };

static const RadioSlot *selectedSlot(void) {
  return &gDeviceConfig.radio.slots[gDeviceConfig.radio.selected_slot];
}

// The slot's remembered frequency, moved into the band's receivable part.
static uint32_t bandFrequency(uint8_t band) {
  uint32_t hz = selectedSlot()->bands[band].frequency_hz;
  RadioSpan span;
  if (!RADIO_MODEL_BandSpan(band, hz, RECEIVER_Capabilities(), &span)) return hz;
  return hz < span.start_hz ? span.start_hz : hz >= span.end_hz ? span.end_hz - 1u : hz;
}

static void enter(void) {
  shownCount = row = 0;
  for (uint8_t i = 0; i < radioBandCount; ++i) {
    if (!RADIO_MODEL_BandSpan(i, radioBands[i].initial_hz, RECEIVER_Capabilities(), NULL)) continue;
    if (i == selectedSlot()->active_band) row = shownCount;
    shown[shownCount++] = i;
  }
  selected = shown[row];
  field = 0;
  editing = false;
  message = NULL;
}

static void save(void) {
  if (!gDeviceConfigReady) {
    message = "Read only";
    return;
  }
  RxPlan plan;
  RxResult result = RADIO_MODEL_Plan(bandFrequency(selected), draft,
                                    RECEIVER_Capabilities(), &plan);
  if (result != RX_OK) {
    message = result == RX_PATCH_MISSING ? "SSB patch missing"
            : result == RX_INVALID_PARAMETERS ? "Invalid value" : "RX unavailable";
    return;
  }
  RadioParameters previous = gDeviceConfig.radio.presets[selected];
  gDeviceConfig.radio.presets[selected] = draft;
  if (!DEVICE_SaveNow()) {
    gDeviceConfig.radio.presets[selected] = previous;
    message = "Save failed";
    return;
  }
  editing = false;
  message = "Saved";
}

static bool key(KEY_Code_t key, Key_State_t state) {
  // Editor never dispatches tuning side keys or PTT.
  if (state != KEY_RELEASED && state != KEY_LONG_PRESSED_CONT)
    return true;
  if (key == KEY_UP || key == KEY_DOWN) {
    uint8_t count = editing ? FIELD_COUNT : shownCount;
    uint8_t *index = editing ? &field : &row;
    *index = (*index + (key == KEY_DOWN ? 1u : count-1u))%count;
    selected = shown[row];
    message = NULL;
    return true;
  }
  if (state != KEY_RELEASED)
    return true;
  if (key == KEY_EXIT) {
    if (editing) {
      editing = false; // Discard the entire draft.
      message = NULL;
    } else {
      SCREEN_Pop();
    }
    return true;
  }
  if (!editing) {
    if (key == KEY_MENU) {
      draft = gDeviceConfig.radio.presets[selected];
      field = 0;
      editing = true;
      message = NULL;
    }
    return true;
  }
  if (key != KEY_MENU && key != KEY_STAR && key != KEY_F)
    return true;
  message = NULL;
  if (field < FIELD_CURRENT) {
    if (!RADIO_MODEL_Adjust(bandFrequency(selected),
                            &draft, RECEIVER_Capabilities(),
                            (RadioParameterField)field, key != KEY_F))
      message = field == RADIO_FIELD_BANDWIDTH && draft.mode == RX_WFM ? "Fixed" : "No option";
  } else if (key == KEY_MENU) {
    if (field == FIELD_CURRENT) {
      if (selected == selectedSlot()->active_band)
        draft = selectedSlot()->current.parameters;
      else
        message = "Not current band";
    } else if (field == FIELD_DEFAULTS) {
      draft = radioBands[selected].preset;
    } else {
      save();
    }
  }
  return true;
}

static void render(void) {
  static const char *const labels[] = {"Mode", "Step", "BW", "SQ", "RX Tone", "Use current", "Reset to defaults", "Save"};
  STATUSLINE_SetText(message ? message : editing ? radioBands[selected].name : "Bands");
  uint8_t index = editing ? field : row;
  uint8_t start = index >= 5 ? index-4 : 0;
  uint8_t count = editing ? FIELD_COUNT : shownCount;
  for (uint8_t line = 0; line < 5 && start+line < count; ++line) {
    uint8_t i = start+line, y = 17+line*11;
    PrintMediumEx(2, y, POS_L, C_FILL, editing ? labels[i] : radioBands[shown[i]].name);
    if (!editing) {
      PrintSmallEx(124, y, POS_R, C_FILL, rxModeNames[gDeviceConfig.radio.presets[shown[i]].mode]);
    } else if (i == RADIO_FIELD_MODE) {
      PrintSmallEx(124, y, POS_R, C_FILL, rxModeNames[draft.mode]);
    } else {
      char value[24];
      Text t = TEXT_Begin(value, sizeof(value));
      if (i == RADIO_FIELD_STEP || (i == RADIO_FIELD_BANDWIDTH && draft.mode != RX_WFM)) {
        TEXT_Number(&t, i == RADIO_FIELD_STEP ? draft.step_hz : draft.bandwidth_hz, 1, 10);
        TEXT_Add(&t, " Hz");
      } else if (i == RADIO_FIELD_BANDWIDTH) TEXT_Add(&t, "Fixed");
      else if (i == RADIO_FIELD_SQUELCH) TEXT_Number(&t, draft.squelch, 1, 10);
      else if (i == RADIO_FIELD_RX_TONE) TONE_Format(draft.rx_tone, value);
      PrintSmallEx(124, y, POS_R, C_FILL, value);
    }
    if (i == index)
      FillRect(0, y-8, 128, 11, C_INVERT);
  }
}

const Screen bandsScreen = { .title = "Bands", .enter = enter, .render = render, .key = key };
