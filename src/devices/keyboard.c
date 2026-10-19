#include "devices/keyboard.h"
#include "hal/scheduler.h"
#include "hal/gpio.h"
#include "hal/board.h"
#include "hal/systick.h"

static void postKey(KEY_Code_t key, Key_State_t state);

bool KEYBOARD_FrequencyIncreases(KEY_Code_t key) {
  return BOARD_HasHorizontalArrows() ? key == KEY_DOWN : key == KEY_UP;
}

static SystemMessages n;

#define LONG_PRESS_TIME 500
#define LONG_PRESS_REPEAT_TIME 100
#define KEYBOARD_ROWS 5
#define KEYBOARD_COLS 4

static bool mKeyPtt;
static KEY_Code_t mKeyPressed = KEY_INVALID;
static KEY_Code_t mPrevKeyPressed = KEY_INVALID;
static Key_State_t mPrevStatePtt;
static Key_State_t mPrevKeyState;
static uint32_t mLongPressTimer;
static uint32_t mLongPressRepeatTimer;

static uint32_t mPttLongPressTimer;
static uint32_t mPttLongPressRepeatTimer;

// Scan order: index 0 with every output high (side keys), then each output
// driven low in turn. Inputs read low while pressed. Both boards share it.
static const KEY_Code_t keyboard[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    {KEY_SIDE1, KEY_SIDE2, KEY_INVALID, KEY_INVALID},
    {KEY_MENU, KEY_1, KEY_4, KEY_7},
    {KEY_UP, KEY_2, KEY_5, KEY_8},
    {KEY_DOWN, KEY_3, KEY_6, KEY_9},
    {KEY_EXIT, KEY_STAR, KEY_0, KEY_F},
};

static void HandlePttKey(void) {
  uint32_t currentTick = Now();
  mKeyPtt = !GPIO_Read(GPIO_PTT);

  if (mKeyPtt) {
    if (mPrevStatePtt == KEY_RELEASED) {
      postKey(KEY_PTT, KEY_PRESSED);
      mPrevStatePtt = KEY_PRESSED;
      mPttLongPressTimer = currentTick;
    } else if (mPrevStatePtt == KEY_PRESSED) {
      uint32_t elapsedTime = currentTick - mPttLongPressTimer;
      if (elapsedTime >= LONG_PRESS_TIME) {
        postKey(KEY_PTT, KEY_LONG_PRESSED);
        mPttLongPressTimer = 0;
        mPrevStatePtt = KEY_LONG_PRESSED;
        mPttLongPressRepeatTimer = currentTick;
      }
    } else if (mPrevStatePtt == KEY_LONG_PRESSED ||
               mPrevStatePtt == KEY_LONG_PRESSED_CONT) {
      uint32_t elapsedTime = currentTick - mPttLongPressRepeatTimer;
      if (elapsedTime >= LONG_PRESS_REPEAT_TIME) {
        mPttLongPressRepeatTimer = currentTick;
        postKey(KEY_PTT, KEY_LONG_PRESSED_CONT);
        mPrevStatePtt = KEY_LONG_PRESSED_CONT;
      }
    }
  } else {
    if (mPrevStatePtt != KEY_RELEASED) {
      if (mPrevStatePtt == KEY_PRESSED) {
        postKey(KEY_PTT, KEY_RELEASED);
      }
      if (mPrevStatePtt == KEY_LONG_PRESSED ||
          mPrevStatePtt == KEY_LONG_PRESSED_CONT) {
        postKey(KEY_PTT, KEY_RELEASED);
      }

      mPttLongPressTimer = 0;
      mPrevStatePtt = KEY_RELEASED;
    }
  }
}

// Every output high, then output row-1 low (none for row 0).
static void DriveRow(uint8_t row) {
  for (uint8_t out = 0; out < KEYBOARD_COLS; ++out)
    GPIO_Write((GpioLine)(GPIO_KEY_OUT0 + out), true);
  if (row) GPIO_Write((GpioLine)(GPIO_KEY_OUT0 + row - 1u), false);
}

static uint8_t ReadInputs(void) {
  uint8_t bits = 0;
  for (uint8_t in = 0; in < KEYBOARD_COLS; ++in)
    bits |= (uint8_t)(GPIO_Read((GpioLine)(GPIO_KEY_IN0 + in)) << in);
  return bits;
}

// Three equal reads in a row, 1 us apart.
static uint8_t ReadStableInputs(void) {
  uint8_t reg = 0;
  for (uint8_t same = 0; same < 3; ++same) {
    TIMER_DelayUs(1);
    uint8_t now = ReadInputs();
    if (now != reg) {
      reg = now;
      same = 0;
    }
  }
  return reg;
}

// Outputs return to the board's idle levels (lines shared with other parts).
static void IdleOutputs(void) {
  for (uint8_t out = 0; out < KEYBOARD_COLS; ++out)
    GPIO_Write((GpioLine)(GPIO_KEY_OUT0 + out), BOARD_KEYPAD_IDLE_LEVELS >> out & 1u);
}

static uint8_t ScanKeyboardMatrix(void) {
  for (uint8_t i = 0; i < KEYBOARD_ROWS; i++) {
    DriveRow(i);
    uint8_t reg = ReadStableInputs();
    for (uint8_t j = 0; j < KEYBOARD_COLS; j++)
      if (!(reg >> j & 1u) && keyboard[i][j] != KEY_INVALID)
        return keyboard[i][j];
  }
  return KEY_INVALID;
}

void KEYBOARD_Poll(void) {
  HandlePttKey();
  mKeyPressed = ScanKeyboardMatrix();
  IdleOutputs();
}

// Latches the key event read by KEYBOARD_GetKey.
static void postKey(KEY_Code_t key, Key_State_t state) {
  n.message = MSG_KEYPRESSED;
  n.key = key;
  n.state = state;
}

void KEYBOARD_CheckKeys(void) {
  uint32_t currentTick = Now();

  if (mKeyPressed != KEY_INVALID) {
    if (mPrevKeyState == KEY_RELEASED) {
      postKey(mKeyPressed, KEY_PRESSED);
      mPrevKeyState = KEY_PRESSED;

      mLongPressTimer = currentTick;
      mPrevKeyPressed = mKeyPressed;
    } else if (mPrevKeyState == KEY_PRESSED) {
      uint32_t elapsedTime = currentTick - mLongPressTimer;
      if (elapsedTime >= LONG_PRESS_TIME) {
        postKey(mKeyPressed, KEY_LONG_PRESSED);
        mLongPressTimer = 0;
        mPrevKeyState = KEY_LONG_PRESSED;
        mLongPressRepeatTimer = currentTick;
      }
    } else if (mPrevKeyState == KEY_LONG_PRESSED ||
               mPrevKeyState == KEY_LONG_PRESSED_CONT) {
      uint32_t elapsedTime = currentTick - mLongPressRepeatTimer;
      if (elapsedTime >= LONG_PRESS_REPEAT_TIME) {
        mLongPressRepeatTimer = currentTick;
        postKey(mKeyPressed, KEY_LONG_PRESSED_CONT);
        mPrevKeyState = KEY_LONG_PRESSED_CONT;
      }
    }
  } else {
    if (mPrevKeyState != KEY_RELEASED) {
      if (mPrevKeyState != KEY_LONG_PRESSED &&
          mPrevKeyState != KEY_LONG_PRESSED_CONT) {
        postKey(mPrevKeyPressed, KEY_RELEASED);
      }

      if (mPrevKeyState == KEY_LONG_PRESSED_CONT &&
          (mPrevKeyPressed == KEY_UP || mPrevKeyPressed == KEY_DOWN)) {
        postKey(mPrevKeyPressed, KEY_RELEASED);
      }

      mLongPressTimer = 0;
      mPrevKeyState = KEY_RELEASED;
      mPrevKeyPressed = KEY_INVALID;
    }
  }
}

SystemMessages KEYBOARD_GetKey(void) {
  n.message = MSG_NONE;
  KEYBOARD_Poll();
  KEYBOARD_CheckKeys();
  return n;
}
