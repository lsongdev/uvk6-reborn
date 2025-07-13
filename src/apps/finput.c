#include "finput.h"
#include "../driver/st7565.h"
#include "../driver/uart.h"
#include "../scheduler.h"
#include "../ui/graphics.h"
#include "apps.h"

static const uint8_t BIG_DIGIT_HEIGHT = 11;

typedef struct {
  uint32_t min;
  uint32_t max;
  InputUnit unit;
  bool is_range;      // Whether we're inputting a range
  uint8_t max_digits; // Maximum allowed digits
} InputConfig;

uint32_t gFInputValue1; // Primary value (or start of range)
uint32_t gFInputValue2; // End of range (if is_range is true)
void (*gFInputCallback)(uint32_t value1, uint32_t value2);

#define MAX_INPUT_LENGTH 10

static char inputBuffer[MAX_INPUT_LENGTH] = "";
static uint8_t cursorPos = 0;
static uint8_t blinkState = 0;
static uint32_t lastUpdate;

static InputConfig currentConfig = {
    .min = 0,
    .max = UINT32_MAX,
    .unit = UNIT_RAW,
    .is_range = false,
    .max_digits = MAX_INPUT_LENGTH,
};

static uint32_t convertFromDisplayValue(uint32_t displayValue) {
  switch (currentConfig.unit) {
  case UNIT_HZ:
    return displayValue * 10; // 10 -> 100Hz
  case UNIT_KHZ:
    return displayValue * 100; // 1234 -> 123400Hz
  case UNIT_MHZ:
    return displayValue * 100000; // 145 -> 14500000Hz
  case UNIT_VOLTS:
    return displayValue; // 824 -> 8.24V (handled by callback)
  case UNIT_DBM:
    return displayValue; // Direct value
  case UNIT_PERCENT:
    return displayValue; // Direct value
  default:
    return displayValue; // Raw value
  }
}

static uint32_t getCurrentValue() {
  uint32_t value = 0;

  for (uint8_t i = 0; i < cursorPos && inputBuffer[i] != '\0'; i++) {
    if (inputBuffer[i] >= '0' && inputBuffer[i] <= '9') {
      value = value * 10 + (inputBuffer[i] - '0');
    }
  }

  return value;
}

static void updateDisplayValue() {
  uint32_t value = getCurrentValue();
  gFInputValue1 = convertFromDisplayValue(value);
}

static void reset() {
  cursorPos = 0;
  blinkState = 0;
  memset(inputBuffer, 0, MAX_INPUT_LENGTH);
}

static void fillFromCurrentValue() {
  uint32_t displayValue;

  switch (currentConfig.unit) {
  case UNIT_HZ:
    displayValue = gFInputValue1 / 10;
    break;
  case UNIT_KHZ:
    displayValue = gFInputValue1 / 100;
    break;
  case UNIT_MHZ:
    displayValue = gFInputValue1 / 100000;
    break;
  case UNIT_VOLTS:
    displayValue = gFInputValue1;
    break;
  default:
    displayValue = gFInputValue1;
    break;
  }

  snprintf(inputBuffer, MAX_INPUT_LENGTH + 1, "%lu", displayValue);
  cursorPos = strlen(inputBuffer);
}

void FINPUT_setup(uint32_t min, uint32_t max, InputUnit unit, bool is_range) {
  currentConfig.min = min;
  currentConfig.max = max;
  currentConfig.unit = unit;
  currentConfig.is_range = is_range;
}

void FINPUT_init() {

  // Set max digits based on unit and max value
  uint32_t maxDisplayValue;
  switch (currentConfig.unit) {
  case UNIT_MHZ:
    maxDisplayValue = currentConfig.max / 100000;
    break;
  case UNIT_KHZ:
    maxDisplayValue = currentConfig.max / 100;
    break;
  case UNIT_HZ:
    maxDisplayValue = currentConfig.max / 10;
    break;
  default:
    maxDisplayValue = currentConfig.max;
    break;
  }

  currentConfig.max_digits = 0;
  while (maxDisplayValue > 0) {
    maxDisplayValue /= 10;
    currentConfig.max_digits++;
  }

  if (currentConfig.max_digits > MAX_INPUT_LENGTH) {
    currentConfig.max_digits = MAX_INPUT_LENGTH;
  }

  reset();
  fillFromCurrentValue();
}

void FINPUT_update() {
  if (Now() - lastUpdate >= 500) {
    blinkState = !blinkState;
    gRedrawScreen = true;
    lastUpdate = Now();
  }
}

void FINPUT_deinit(void) {}

bool FINPUT_key(KEY_Code_t key, Key_State_t state) {
  if (state == KEY_LONG_PRESSED && key == KEY_EXIT) {
    reset();
    return true;
  }

  if (state == KEY_RELEASED) {
    switch (key) {
    case KEY_0:
    case KEY_1:
    case KEY_2:
    case KEY_3:
    case KEY_4:
    case KEY_5:
    case KEY_6:
    case KEY_7:
    case KEY_8:
    case KEY_9:
      if (cursorPos < currentConfig.max_digits) {
        inputBuffer[cursorPos++] = '0' + (key - KEY_0);
        inputBuffer[cursorPos] = '\0';
        updateDisplayValue();
        gRedrawScreen = true;
      }
      return true;

    case KEY_STAR:
      if (currentConfig.is_range && cursorPos > 0) {
        // Save first value and start entering second
        gFInputValue1 = convertFromDisplayValue(getCurrentValue());
        reset();
        return true;
      }
      break;

    case KEY_EXIT:
      if (cursorPos == 0) {
        if (currentConfig.is_range && gFInputValue1 != 0) {
          // We were entering second value, go back to first
          uint32_t temp = gFInputValue1;
          reset();
          gFInputValue1 = 0;
          fillFromCurrentValue();
          return true;
        }

        gFInputCallback = NULL;
        APPS_exit();
        return true;
      }

      // Backspace
      if (cursorPos > 0) {
        inputBuffer[--cursorPos] = '\0';
        updateDisplayValue();
        gRedrawScreen = true;
      }
      return true;

    case KEY_MENU:
    case KEY_F:
    case KEY_PTT:
      if (gFInputValue1 >= currentConfig.min &&
          gFInputValue1 <= currentConfig.max) {

        if (currentConfig.is_range) {
          // If we're in range mode and haven't entered second value, use first
          // value for both
          if (cursorPos > 0) {
            gFInputValue2 = convertFromDisplayValue(getCurrentValue());
          } else {
            gFInputValue2 = gFInputValue1;
          }
        }

        APPS_exit();
        if (gFInputCallback) {
          gFInputCallback(gFInputValue1,
                          currentConfig.is_range ? gFInputValue2 : 0);
        }
        gFInputCallback = NULL;
        gFInputValue1 = 0;
        gFInputValue2 = 0;
      }
      return true;

    default:
      break;
    }
  }
  return false;
}

void FINPUT_render(void) {
  const uint8_t BASE_Y = 24;
  char displayStr[MAX_INPUT_LENGTH + 3] =
      ""; // Extra space for possible range separator

  // Prepare display string
  strncpy(displayStr, inputBuffer, MAX_INPUT_LENGTH);

  // Add range separator if we're in range mode and have first value
  if (currentConfig.is_range && gFInputValue1 != 0 && cursorPos > 0) {
    strncat(displayStr, "-", 1);
  }

  // Add unit suffix
  const char *unitSuffix = "";
  switch (currentConfig.unit) {
  case UNIT_HZ:
    unitSuffix = "Hz";
    break;
  case UNIT_KHZ:
    unitSuffix = "kHz";
    break;
  case UNIT_MHZ:
    unitSuffix = "MHz";
    break;
  case UNIT_VOLTS:
    unitSuffix = "V";
    break;
  case UNIT_DBM:
    unitSuffix = "dBm";
    break;
  case UNIT_PERCENT:
    unitSuffix = "%";
    break;
  default:
    unitSuffix = "";
    break;
  }

  // Display the input with blinking cursor
  PrintBigDigitsEx(LCD_WIDTH - 3, BASE_Y, POS_R, C_FILL, "%s", displayStr);
  PrintMediumEx(LCD_WIDTH - 3, BASE_Y + 8, POS_R, C_FILL, "%s", unitSuffix);

  // Show cursor if not at max length
  if (blinkState && cursorPos < currentConfig.max_digits) {
    // Draw cursor at current position
    FillRect(LCD_WIDTH - 1, BASE_Y - BIG_DIGIT_HEIGHT + 1, 1, BIG_DIGIT_HEIGHT,
             C_FILL);
  }

  // Show range info if applicable
  if (currentConfig.is_range && gFInputValue1 != 0) {
    char rangeStr[20];
    if (cursorPos == 0) {
      // Showing first value before entering second
      snprintf(rangeStr, sizeof(rangeStr), "Range: %lu", gFInputValue1);
    } else {
      // Showing both values
      uint32_t displayValue2 = convertFromDisplayValue(getCurrentValue());
      snprintf(rangeStr, sizeof(rangeStr), "Range: %lu-%lu", gFInputValue1,
               displayValue2);
    }
    PrintSmall(0, 0, rangeStr);
  }
}
