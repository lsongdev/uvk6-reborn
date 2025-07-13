#include "finput.h"
#include "../driver/st7565.h"
#include "../driver/uart.h"
#include "../scheduler.h"
#include "../ui/graphics.h"
#include "apps.h"

// Константы для преобразования частот
#define HZ 1       // 1 * 10Hz
#define KHZ 100    // 100 * 10Hz = 1kHz
#define MHZ 100000 // 100000 * 10Hz = 1MHz

static const uint8_t BIG_DIGIT_HEIGHT = 11;
static const uint8_t BIG_DIGIT_WIDTH = 11;
static const uint8_t MAX_FRACTION_DIGITS = 6;

typedef struct {
  uint32_t min;
  uint32_t max;
  InputUnit unit;
  bool is_range;
  uint8_t max_digits;
  bool allow_dot;
  uint32_t multiplier; // Множитель для преобразования в 10Hz
} InputConfig;

uint32_t gFInputValue1;
uint32_t gFInputValue2;
void (*gFInputCallback)(uint32_t value1, uint32_t value2);

#define MAX_INPUT_LENGTH 10

static char inputBuffer[MAX_INPUT_LENGTH] = "";
static uint8_t cursorPos = 0;
static uint8_t blinkState = 0;
static uint32_t lastUpdate;
static bool dotEntered = false;
static uint8_t fractionalDigits = 0;

static InputConfig currentConfig = {.min = 0,
                                    .max = UINT32_MAX,
                                    .unit = UNIT_RAW,
                                    .is_range = false,
                                    .max_digits = MAX_INPUT_LENGTH,
                                    .allow_dot = false,
                                    .multiplier = 1};

static const uint32_t POW10[] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000,
};

static uint32_t convertFromDisplayValue() {
  uint32_t integerPart = 0;
  uint32_t fractionalPart = 0;
  bool isFractional = false;
  fractionalDigits = 0;

  for (uint8_t i = 0; i < cursorPos && inputBuffer[i] != '\0'; i++) {
    if (inputBuffer[i] == '.') {
      isFractional = true;
    } else if (inputBuffer[i] >= '0' && inputBuffer[i] <= '9') {
      if (isFractional) {
        fractionalPart = fractionalPart * 10 + (inputBuffer[i] - '0');
        fractionalDigits++;
      } else {
        integerPart = integerPart * 10 + (inputBuffer[i] - '0');
      }
    }
  }

  // Для частот используем множитель (храним в 10Hz)
  if (currentConfig.unit == UNIT_HZ || currentConfig.unit == UNIT_KHZ ||
      currentConfig.unit == UNIT_MHZ) {
    return (integerPart * currentConfig.multiplier) +
           (fractionalPart * currentConfig.multiplier /
            POW10[fractionalDigits]);
  }

  // Для напряжения и других величин
  switch (currentConfig.unit) {
  case UNIT_VOLTS:
    return integerPart * 100 + fractionalPart; // 8.24V -> 824
  default:
    return integerPart;
  }
}

static void reset() {
  cursorPos = 0;
  blinkState = 0;
  dotEntered = false;
  fractionalDigits = 0;
  memset(inputBuffer, 0, MAX_INPUT_LENGTH);
}

static void fillFromCurrentValue() {
  uint32_t value = gFInputValue1;
  uint32_t integerPart = 0;
  uint32_t fractionalPart = 0;
  fractionalDigits = 0;

  switch (currentConfig.unit) {
  case UNIT_HZ:
    integerPart = value / (HZ);
    fractionalPart = value % (HZ);
    fractionalDigits = 1;
    break;
  case UNIT_KHZ:
    integerPart = value / (KHZ);
    fractionalPart = value % (KHZ);
    fractionalDigits = 3;
    break;
  case UNIT_MHZ:
    integerPart = value / (MHZ);
    fractionalPart = value % (MHZ);
    fractionalDigits = 5;
    break;
  case UNIT_VOLTS:
    integerPart = value / 100;
    fractionalPart = value % 100;
    fractionalDigits = 2;
    break;
  default:
    integerPart = value;
    break;
  }

  if (fractionalDigits > 0 && fractionalPart > 0) {
    // Убираем trailing zeros
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

  // Устанавливаем множитель для преобразования
  switch (unit) {
  case UNIT_HZ:
    currentConfig.multiplier = HZ;
    break;
  case UNIT_KHZ:
    currentConfig.multiplier = KHZ;
    break;
  case UNIT_MHZ:
    currentConfig.multiplier = MHZ;
    break;
  default:
    currentConfig.multiplier = 1;
    break;
  }
}

void FINPUT_init() {
  uint32_t maxDisplayValue;
  switch (currentConfig.unit) {
  case UNIT_MHZ:
    maxDisplayValue = currentConfig.max / MHZ;
    break;
  case UNIT_KHZ:
    maxDisplayValue = currentConfig.max / KHZ;
    break;
  case UNIT_HZ:
    maxDisplayValue = currentConfig.max / HZ;
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

  if (currentConfig.max_digits > MAX_INPUT_LENGTH - 1) {
    currentConfig.max_digits = MAX_INPUT_LENGTH - 1;
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
    gRedrawScreen = true;
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
      if (cursorPos < MAX_INPUT_LENGTH - 1) {
        if (dotEntered && fractionalDigits >= MAX_FRACTION_DIGITS) {
          break;
        }
        inputBuffer[cursorPos++] = '0' + (key - KEY_0);
        inputBuffer[cursorPos] = '\0';
        if (dotEntered)
          fractionalDigits++;
        gFInputValue1 = convertFromDisplayValue();
        gRedrawScreen = true;
      }
      return true;

    case KEY_STAR:
      if (currentConfig.allow_dot && !dotEntered && cursorPos > 0 &&
          cursorPos < MAX_INPUT_LENGTH - 1) {
        inputBuffer[cursorPos++] = '.';
        inputBuffer[cursorPos] = '\0';
        dotEntered = true;
        gRedrawScreen = true;
        return true;
      }
      if (currentConfig.is_range && cursorPos > 0) {
        gFInputValue1 = convertFromDisplayValue();
        reset();
        gRedrawScreen = true;
        return true;
      }
      break;

    case KEY_EXIT:
      if (cursorPos == 0) {
        if (currentConfig.is_range && gFInputValue1 != 0) {
          uint32_t temp = gFInputValue1;
          reset();
          gFInputValue1 = temp;
          fillFromCurrentValue();
          gRedrawScreen = true;
          return true;
        }
        gFInputCallback = NULL;
        APPS_exit();
        return true;
      }

      if (cursorPos > 0) {
        if (inputBuffer[cursorPos - 1] == '.') {
          dotEntered = false;
        } else if (dotEntered && fractionalDigits > 0) {
          fractionalDigits--;
        }
        inputBuffer[--cursorPos] = '\0';
        gFInputValue1 = convertFromDisplayValue();
        gRedrawScreen = true;
      }
      return true;

    case KEY_MENU:
    case KEY_F:
    case KEY_PTT:
      if (gFInputValue1 >= currentConfig.min &&
          gFInputValue1 <= currentConfig.max) {
        if (currentConfig.is_range) {
          if (cursorPos > 0) {
            gFInputValue2 = convertFromDisplayValue();
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
  char displayStr[MAX_INPUT_LENGTH + 3] = "";

  strncpy(displayStr, inputBuffer, MAX_INPUT_LENGTH);

  if (currentConfig.is_range && gFInputValue1 != 0 && cursorPos > 0) {
    strncat(displayStr, "-", 1);
  }

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

  PrintBigDigitsEx(LCD_WIDTH - 3, BASE_Y, POS_R, C_FILL, "%s", displayStr);
  PrintMediumEx(LCD_WIDTH - 3, BASE_Y + 8, POS_R, C_FILL, "%s", unitSuffix);

  if (blinkState && cursorPos < currentConfig.max_digits +
                                    (dotEntered ? MAX_FRACTION_DIGITS : 0)) {
    uint8_t cursorX =
        LCD_WIDTH - 3 - (strlen(displayStr) - cursorPos) * BIG_DIGIT_WIDTH;
    FillRect(cursorX, BASE_Y - BIG_DIGIT_HEIGHT + 1, 1, BIG_DIGIT_HEIGHT,
             C_FILL);
  }

  if (currentConfig.is_range && gFInputValue1 != 0) {
    char rangeStr[20];
    if (cursorPos == 0) {
      snprintf(rangeStr, sizeof(rangeStr), "Range: %lu", gFInputValue1);
    } else {
      uint32_t displayValue2 = convertFromDisplayValue();
      snprintf(rangeStr, sizeof(rangeStr), "Range: %lu-%lu", gFInputValue1,
               displayValue2);
    }
    PrintSmall(0, 0, rangeStr);
  }
}
