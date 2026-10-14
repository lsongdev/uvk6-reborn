#ifndef UI_SCREENS_SCREEN_H
#define UI_SCREENS_SCREEN_H
#include "hal/keyboard.h"
#include <stdbool.h>

// A page on the screen stack. Only the top screen draws and takes keys; the
// radio keeps running underneath whatever is shown. enter runs when a
// screen becomes the top (pushed, or uncovered by a pop) and leave when it
// stops being the top (covered, or popped). Every hook is optional.
typedef struct {
  const char *title;
  void (*enter)(void);
  void (*leave)(void);
  void (*update)(void);
  void (*render)(void);
  bool (*key)(KEY_Code_t key, Key_State_t state); // True when handled.
} Screen;

void SCREEN_Reset(const Screen *root); // Drop the stack and show root.
void SCREEN_Push(const Screen *screen);
bool SCREEN_Pop(void);                 // False at the root.
const Screen *SCREEN_Top(void);
bool SCREEN_Key(KEY_Code_t key, Key_State_t state);
void SCREEN_Update(void);
void SCREEN_Render(void);

#endif
