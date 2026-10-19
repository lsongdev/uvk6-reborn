// T9-style text entry for names: digits pick a letter row, then a letter.
#include "ui/screens/text-input.h"
#include "devices/display.h"
#include "ui/graphics.h"
#include "core/text.h"
#include <string.h>

static char *target;            // Caller's buffer, written on accept.
static uint8_t limit;           // Longest accepted text.
static void (*accepted)(void);

static const char *letters[9] = {
    "",
    "abc",  // 2
    "def",  // 3
    "ghi",  // 4
    "jkl",  // 5
    "mno",  // 6
    "pqrs", // 7
    "tuv",  // 8
    "wxyz"  // 9
};

static const char *lettersCapital[9] = {
    "",
    "ABC",  // 2
    "DEF",  // 3
    "GHI",  // 4
    "JKL",  // 5
    "MNO",  // 6
    "PQRS", // 7
    "TUV",  // 8
    "WXYZ"  // 9
};

static const char *numbers[10] = {"1", "2", "3", "4", "5",
                                  "6", "7", "8", "9", "0"};
static const char *symbols[9] = {
    "",
    ".,!?:;",   // 2
    "()[]<>{}", // 3
    "#@&$%",    // 4
    "+-*/=^~`", // 5
    "\"'",      // 6
    "",         // 7
    "",         // 8
    ""          // 9
};

static const char **currentSet = lettersCapital;
static const char *currentRow;
#define FIELD_BYTES 17 // Longest caller buffer: a 16-byte channel name.
static char inputField[FIELD_BYTES] = {0};
static uint8_t inputIndex = 0;
static bool coursorBlink = true;

static void insert(char c) {
  if (inputField[inputIndex] != '\0') {
    memmove(inputField + inputIndex + 1, inputField + inputIndex,
            FIELD_BYTES - 1 - inputIndex);
  }
  inputField[inputIndex++] = c;
}

static void backspace(void) {
  if (inputField[inputIndex] != '\0') {
    inputIndex--;
    memmove(inputField + inputIndex, inputField + inputIndex + 1,
            FIELD_BYTES - 1 - inputIndex);

  } else {
    inputField[--inputIndex] = '\0';
  }
}

void TEXT_INPUT_Open(char *text, uint8_t max, void (*onAccept)(void), bool replaceTop) {
  target = text;
  limit = max < FIELD_BYTES - 1 ? max : FIELD_BYTES - 1;
  accepted = onAccept;
  if (replaceTop) SCREEN_Pop();
  SCREEN_Push(&textInputScreen);
}

static void enter(void) {
  strncpy(inputField, target, limit);
  inputField[limit] = '\0';
  inputIndex = strlen(inputField);
}

static void update(void) {
  coursorBlink = !coursorBlink;
  gRedrawScreen = true;
}


static bool key(KEY_Code_t key, Key_State_t state) {
  // up-down keys
  if (state == KEY_RELEASED || state == KEY_LONG_PRESSED) {
    switch (key) {
    case KEY_UP:
      if (inputField[inputIndex] != '\0') {
        inputIndex++;
      }
      return true;
    case KEY_DOWN:
      if (inputIndex > 0) {
        inputIndex--;
      }
      return true;
    default:
      break;
    }
  }

  if (state == KEY_LONG_PRESSED) {
    switch (key) {
    case KEY_EXIT:
      memset(inputField, 0, sizeof(inputField));
      inputIndex = 0;
      return true;
    default:
      break;
    }
  }

  // Simple keypress
  if (state == KEY_RELEASED) {
    switch (key) {
    case KEY_1:
    case KEY_2:
    case KEY_3:
    case KEY_4:
    case KEY_5:
    case KEY_6:
    case KEY_7:
    case KEY_8:
    case KEY_9:
      if (currentSet == numbers) {
        if (strlen(inputField) < limit) {
          insert(key + '0');
        }
        return true;
      }
      if (currentRow) {
        if (key - KEY_1 < (int)strlen(currentRow)) {
          if (strlen(inputField) < limit) {
            insert(currentRow[key - KEY_1]);
          }
          currentRow = NULL;
        }
        return true;
      }
      if (key == KEY_1) {
        if (strlen(inputField) < limit) {
          insert(' ');
        }
      } else {
        currentRow = currentSet[key - KEY_1];
      }
      return true;
    case KEY_0:
      if (currentSet == numbers && strlen(inputField) < limit) {
        insert('0');
        return true;
      }
      if (!currentRow) {
        currentSet = currentSet == numbers ? symbols : numbers;
        return true;
      }
      return true;
    case KEY_STAR:
      if (currentSet != symbols) {
        currentSet = symbols;
        return true;
      }
      return true;
    case KEY_F:
      currentSet = currentSet == lettersCapital ? letters : lettersCapital;
      return true;
    case KEY_EXIT:
      if (currentRow) {
        currentRow = NULL;
      } else {
        if (inputIndex) {
          backspace();
        } else {
          SCREEN_Pop();
        }
      }
      return true;
    case KEY_MENU:
      strcpy(target, inputField);
      SCREEN_Pop();
      if (accepted) accepted();
      return true;
    default:
      break;
    }
  }

  return false;
}

static void render(void) {
  char String[8];
  const uint8_t INPUT_Y = 8 + 14;
  const uint8_t CHAR_W = 5;
  const size_t charCount = strlen(inputField);

  DrawHLine(8, INPUT_Y, LCD_WIDTH - 16, C_FILL);

  Text count = TEXT_Begin(String, sizeof(String));
  TEXT_Number(&count, charCount, 1, 10);
  TEXT_Char(&count, '/');
  TEXT_Number(&count, limit, 1, 10);
  PrintSmallEx(LCD_WIDTH - 8, INPUT_Y + 5, POS_R, C_FILL, String);

  for (size_t i = 0; i < charCount; ++i) {
    char glyph[2] = {inputField[i], 0};
    PrintMedium(8 + i * (CHAR_W + 1), 8 + 12, glyph);
  }

  uint8_t rowStrlen = 0;
  if (currentRow) {
    rowStrlen = strlen(currentRow);
  }

  if (coursorBlink) {
    DrawVLine(8 + (inputIndex * (CHAR_W + 1)) - 1, INPUT_Y - 8, 7, C_FILL);
  }

  for (uint8_t y = 0; y < 3; y++) {
    for (uint8_t x = 0; x < 3; x++) {
      const uint8_t idx = y * 3 + x;
      const uint8_t xPos = x * 43 + 1;
      const uint8_t line = y + 3;
      const uint8_t yPos = line * 8 + 12;

      char key[2] = {'1' + idx, 0};
      PrintMedium(xPos, yPos, key);
      FillRect(xPos - 1, yPos - 6, 7, 8, C_INVERT);

      if (currentRow) {
        if (idx < rowStrlen) {
          char glyph[2] = {currentRow[idx], 0};
          PrintMedium(xPos + 10, line * 8 + 12, glyph);
        }
      } else {
        strcpy(String, currentSet[idx]);
        String[4] = '\0';
        PrintMedium(xPos + 10, line * 8 + 12, String);
      }
    }
  }

  PrintMedium(1, LCD_HEIGHT - 2, "*");
  PrintMedium(11, LCD_HEIGHT - 2, ".,/*");
  FillRect(0, LCD_HEIGHT - 9, 7, 9, C_INVERT);

  PrintMedium(1 + 43, LCD_HEIGHT - 2, "0");
  PrintMedium(11 + 43, LCD_HEIGHT - 2, "123");
  FillRect(0 + 43, LCD_HEIGHT - 9, 7, 9, C_INVERT);

  PrintMedium(1 + 86, LCD_HEIGHT - 2, "#");
  PrintMedium(11 + 86, LCD_HEIGHT - 2,
              currentSet != lettersCapital ? "ABC" : "abc");
  FillRect(0 + 86, LCD_HEIGHT - 9, 7, 9, C_INVERT);
}

const Screen textInputScreen = {
  .title = "Text input", .enter = enter, .update = update, .render = render, .key = key,
};
