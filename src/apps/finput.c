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
  bool allow_dot;     // Whether to allow decimal point input
} InputConfig;

uint32_t gFInputValue1; // Primary value (or start of range)
uint32_t gFInputValue2; // End of range (if is_range is true)
void (*gFInputCallback)(uint32_t value1, uint32_t value2);

#define MAX_INPUT_LENGTH 10

static char inputBuffer[MAX_INPUT_LENGTH] = "";
static uint8_t cursorPos = 0;
static uint8_t blinkState = 0;
static uint32_t lastUpdate;
static bool dotEntered = false;

static InputConfig currentConfig = {
    .min = 0,
    .max = UINT32_MAX,
    .unit = UNIT_RAW,
    .is_range = false,
    .max_digits = MAX_INPUT_LENGTH,
    .allow_dot = false,
};

static uint32_t getCurrentValue() {
  uint32_t value = 0;

  for (uint8_t i = 0; i < cursorPos && inputBuffer[i] != '\0'; i++) {
    if (inputBuffer[i] >= '0' && inputBuffer[i] <= '9') {
      value = value * 10 + (inputBuffer[i] - '0');
    }
  }

  return value;
}

static uint32_t convertFromDisplayValue(uint32_t integerPart,
                                        uint32_t fractionalPart,
                                        uint8_t fractionalDigits) {
  switch (currentConfig.unit) {
  case UNIT_HZ:
    return integerPart * 10 + fractionalPart; // 10.5 -> 105Hz
  case UNIT_KHZ:
    return integerPart * 1000 + fractionalPart * 10; // 123.4 -> 123400Hz
  case UNIT_MHZ:
    return integerPart * 1000000 + fractionalPart * 100; // 27.135 -> 27135000Hz
  case UNIT_VOLTS:
    return integerPart * 100 + fractionalPart; // 8.24 -> 824
  default:
    return integerPart; // Raw value
  }
}

static void parseInputBuffer(uint32_t *integerPart, uint32_t *fractionalPart,
                             uint8_t *fractionalDigits) {
  *integerPart = 0;
  *fractionalPart = 0;
  *fractionalDigits = 0;
  bool isFractional = false;

  for (uint8_t i = 0; i < cursorPos && inputBuffer[i] != '\0'; i++) {
    if (inputBuffer[i] == '.') {
      isFractional = true;
    } else if (inputBuffer[i] >= '0' && inputBuffer[i] <= '9') {
      if (isFractional) {
        *fractionalPart = *fractionalPart * 10 + (inputBuffer[i] - '0');
        (*fractionalDigits)++;
      } else {
        *integerPart = *integerPart * 10 + (inputBuffer[i] - '0');
      }
    }
  }
}

static void updateDisplayValue() {
  uint32_t integerPart, fractionalPart;
  uint8_t fractionalDigits;

  parseInputBuffer(&integerPart, &fractionalPart, &fractionalDigits);
  gFInputValue1 =
      convertFromDisplayValue(integerPart, fractionalPart, fractionalDigits);
}

static void updateDisplayValue2() {
  uint32_t integerPart, fractionalPart;
  uint8_t fractionalDigits;

  parseInputBuffer(&integerPart, &fractionalPart, &fractionalDigits);
  gFInputValue2 =
      convertFromDisplayValue(integerPart, fractionalPart, fractionalDigits);
}

static void reset() {
  cursorPos = 0;
  blinkState = 0;
  dotEntered = false;
  memset(inputBuffer, 0, MAX_INPUT_LENGTH);
}

static void fillFromCurrentValue() {
  uint32_t integerPart = 0;
  uint32_t fractionalPart = 0;
  uint8_t fractionalDigits = 0;

  switch (currentConfig.unit) {
  case UNIT_HZ:
    integerPart = gFInputValue1 / 10;
    fractionalPart = gFInputValue1 % 10;
    fractionalDigits = 1;
    break;
  case UNIT_KHZ:
    integerPart = gFInputValue1 / 1000;
    fractionalPart = (gFInputValue1 % 1000) / 10;
    fractionalDigits = 2;
    break;
  case UNIT_MHZ:
    integerPart = gFInputValue1 / 1000000;
    fractionalPart = (gFInputValue1 % 1000000) / 100;
    fractionalDigits = 4;
    break;
  case UNIT_VOLTS:
    integerPart = gFInputValue1 / 100;
    fractionalPart = gFInputValue1 % 100;
    fractionalDigits = 2;
    break;
  default:
    integerPart = gFInputValue1;
    break;
  }

  if (fractionalDigits > 0 && fractionalPart > 0) {
    // Remove trailing zeros
    while (fractionalPart % 10 == 0 && fractionalDigits > 0) {
      fractionalPart /= 10;
      fractionalDigits--;
    }
    snprintf(inputBuffer, MAX_INPUT_LENGTH + 1, "%lu.%0*lu", integerPart,
             fractionalDigits, fractionalPart);
  } else {
    snprintf(inputBuffer, MAX_INPUT_LENGTH + 1, "%lu", integerPart);
  }
  cursorPos = strlen(inputBuffer);
  dotEntered = strchr(inputBuffer, '.') != NULL;
}

void FINPUT_setup(uint32_t min, uint32_t max, InputUnit unit, bool is_range) {
  currentConfig.min = min;
  currentConfig.max = max;
  currentConfig.unit = unit;
  currentConfig.is_range = is_range;
  currentConfig.allow_dot =
      (unit == UNIT_MHZ || unit == UNIT_KHZ || unit == UNIT_VOLTS);
}

void FINPUT_init() {
  // Set max digits based on unit and max value
  uint32_t maxDisplayValue;
  switch (currentConfig.unit) {
  case UNIT_MHZ:
    maxDisplayValue = currentConfig.max / 1000000;
    break;
  case UNIT_KHZ:
    maxDisplayValue = currentConfig.max / 1000;
    break;
  case UNIT_HZ:
    maxDisplayValue = currentConfig.max / 10;
    break;
  case UNIT_VOLTS:
    maxDisplayValue = currentConfig.max / 100;
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

  if (currentConfig.max_digits >
      MAX_INPUT_LENGTH - 1) { // -1 to account for possible dot
    currentConfig.max_digits = MAX_INPUT_LENGTH - 1;
  }

  reset();
  fillFromCurrentValue();
}

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
      if (cursorPos <
          currentConfig.max_digits +
              (dotEntered ? 4 : 0)) { // Allow extra digits after dot
        inputBuffer[cursorPos++] = '0' + (key - KEY_0);
        inputBuffer[cursorPos] = '\0';
        updateDisplayValue();
        gRedrawScreen = true;
      }
      return true;

    case KEY_STAR:
      if (currentConfig.allow_dot && !dotEntered && cursorPos > 0 &&
          cursorPos < currentConfig.max_digits) {
        // Add decimal point
        inputBuffer[cursorPos++] = '.';
        inputBuffer[cursorPos] = '\0';
        dotEntered = true;
        gRedrawScreen = true;
        return true;
      }
      if (currentConfig.is_range && cursorPos > 0) {
        // Save first value and start entering second
        gFInputValue1 = convertFromDisplayValue(
            0, 0, 0); // Will be updated by parseInputBuffer
        updateDisplayValue();
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
          gFInputValue1 = temp;
          fillFromCurrentValue();
          return true;
        }

        gFInputCallback = NULL;
        APPS_exit();
        return true;
      }

      // Backspace
      if (cursorPos > 0) {
        if (inputBuffer[cursorPos - 1] == '.') {
          dotEntered = false;
        }
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
            // gFInputValue2 = convertFromDisplayValue(getCurrentValue());
            updateDisplayValue2();
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

void FINPUT_update() {
  if (Now() - lastUpdate >= 500) {
    blinkState = !blinkState;
    gRedrawScreen = true;
    lastUpdate = Now();
  }
}

void FINPUT_deinit(void) {}

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
      /* uint32_t displayValue2 = convertFromDisplayValue(getCurrentValue());
      snprintf(rangeStr, sizeof(rangeStr), "Range: %lu-%lu", gFInputValue1,
               displayValue2); */
    }
    PrintSmall(0, 0, rangeStr);
  }
}
