#ifndef UI_SCREENS_STATUSLINE_H
#define UI_SCREENS_STATUSLINE_H

#include "radio/receiver.h"

void STATUSLINE_update();
void STATUSLINE_render();
void STATUSLINE_SetText(const char *text);
void STATUSLINE_SetShortcut(bool pending); // The F prefix awaits its key.

#endif /* end of include guard: UI_SCREENS_STATUSLINE_H */
