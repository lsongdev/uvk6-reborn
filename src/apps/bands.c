#include "apps/bands.h"
#include "apps/apps.h"
#include "core/device-config.h"
#include "core/radio.h"
#include "ui/graphics.h"
#include "ui/statusline.h"

static uint8_t selected, field;
static bool editing;
static RadioParameters draft;
static const char *message;

enum { FIELD_CURRENT = 5, FIELD_DEFAULTS, FIELD_SAVE, FIELD_COUNT };

static const VfoConfig *selectedVfo(void) {
  return &gDeviceConfig.radio.vfos[gDeviceConfig.radio.selected_vfo];
}

void BANDS_init(void) {
  selected = selectedVfo()->active_band;
  field = 0;
  editing = false;
  message = NULL;
}

static void save(void) {
  if (!gDeviceConfigReady) {
    message = "Config not ready";
    return;
  }
  RxPlan plan;
  RxResult result = RADIO_MODEL_Plan(selectedVfo()->bands[selected].frequency_hz,
                                    draft, RADIO_GetReceiverCapabilities(), &plan);
  if (result != RX_OK) {
    message = result == RX_PATCH_MISSING ? "SSB patch missing"
            : result == RX_INVALID_PARAMETERS ? "Invalid params" : "RX unavailable";
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
  message = "Saved: next band entry";
}

bool BANDS_key(KEY_Code_t key, Key_State_t state) {
  // Editor never dispatches tuning side keys or PTT.
  if (state != KEY_RELEASED && state != KEY_LONG_PRESSED_CONT)
    return true;
  if (key == KEY_UP || key == KEY_DOWN) {
    uint8_t count = editing ? FIELD_COUNT : radioBandCount;
    uint8_t *index = editing ? &field : &selected;
    *index = (*index + (key == KEY_DOWN ? 1 : count-1))%count;
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
      APPS_exit();
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
    if (!RADIO_MODEL_Adjust(selectedVfo()->bands[selected].frequency_hz,
                            &draft, RADIO_GetReceiverCapabilities(),
                            (RadioParameterField)field, key != KEY_F))
      message = field == RADIO_FIELD_BANDWIDTH && draft.mode == RX_WFM ? "Fixed bandwidth" : "No valid option";
  } else if (key == KEY_MENU) {
    if (field == FIELD_CURRENT) {
      if (selected == selectedVfo()->active_band)
        draft = selectedVfo()->current.parameters;
      else
        message = "Select current band";
    } else if (field == FIELD_DEFAULTS) {
      draft = radioBands[selected].preset;
    } else {
      save();
    }
  }
  return true;
}

void BANDS_render(void) {
  static const char *const labels[] = {"Mode", "Step", "BW", "SQ", "RX Tone", "Use current", "Defaults", "Save"};
  STATUSLINE_SetText(editing ? radioBands[selected].name : "Bands");
  uint8_t index = editing ? field : selected;
  uint8_t start = index >= 4 ? index-3 : 0;
  uint8_t count = editing ? FIELD_COUNT : radioBandCount;
  for (uint8_t row = 0; row < 4 && start+row < count; ++row) {
    uint8_t i = start+row, y = 18+row*11;
    PrintMediumEx(2, y, POS_L, C_FILL, "%s", editing ? labels[i] : radioBands[i].name);
    if (!editing) {
      PrintSmallEx(124, y, POS_R, C_FILL, "%s", rxModeNames[gDeviceConfig.radio.presets[i].mode]);
    } else if (i == RADIO_FIELD_MODE) {
      PrintSmallEx(124, y, POS_R, C_FILL, "%s", rxModeNames[draft.mode]);
    } else if (i == RADIO_FIELD_STEP) {
      PrintSmallEx(124, y, POS_R, C_FILL, "%u Hz", draft.step_hz);
    } else if (i == RADIO_FIELD_BANDWIDTH) {
      if (draft.mode == RX_WFM)
        PrintSmallEx(124, y, POS_R, C_FILL, "Fixed");
      else
        PrintSmallEx(124, y, POS_R, C_FILL, "%u Hz", draft.bandwidth_hz);
    } else if (i == RADIO_FIELD_SQUELCH) {
      PrintSmallEx(124, y, POS_R, C_FILL, "%u", draft.squelch);
    } else if (i == RADIO_FIELD_RX_TONE) {
      char toneText[24];
      TONE_Format(draft.rx_tone, toneText);
      PrintSmallEx(124,y,POS_R,C_FILL,"%s",toneText);
    }
    if (i == index)
      FillRect(0, y-8, 128, 11, C_INVERT);
  }
  if (message)
    PrintSmall(2, 61, "%s", message);
}
