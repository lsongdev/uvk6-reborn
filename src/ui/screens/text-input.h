#ifndef UI_SCREENS_TEXT_INPUT_H
#define UI_SCREENS_TEXT_INPUT_H
#include "ui/screens/screen.h"
#include <stdbool.h>
#include <stdint.h>

// Edits text (at most max characters) in place. On MENU the text is written
// back and onAccept runs; EXIT on an empty draft leaves it untouched.
// replaceTop pops the current screen first, so finishing returns below it.
void TEXT_INPUT_Open(char *text, uint8_t max, void (*onAccept)(void), bool replaceTop);
extern const Screen textInputScreen;
#endif
