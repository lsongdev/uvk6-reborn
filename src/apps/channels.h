#ifndef APPS_CHANNELS_H
#define APPS_CHANNELS_H
#include "hal/keyboard.h"
#include <stdbool.h>

// Settings → Channels: browse saved channels, rename or delete them.
// Editing parameters goes through a slot: select the channel (MR), change
// it, then Save as channel.
void CHANNELS_init(void);
void CHANNELS_render(void);
bool CHANNELS_key(KEY_Code_t key, Key_State_t state);
#endif
