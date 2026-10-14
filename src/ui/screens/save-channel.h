#ifndef UI_SCREENS_SAVE_CHANNEL_H
#define UI_SCREENS_SAVE_CHANNEL_H
#include "ui/screens/screen.h"

// Save as channel: choose a number (MR offers its own channel, VFO the first
// empty one), confirm an overwrite, then name it in text input and save.
void SAVE_CHANNEL_Open(void); // Refuses with a notice when the store is unavailable or full.
extern const Screen saveChannelScreen;
#endif
