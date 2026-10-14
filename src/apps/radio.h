#ifndef RADIO_APP_H
#define RADIO_APP_H

#include "hal/keyboard.h"
#include <stdbool.h>
#include <stdint.h>

bool RADIO_APP_isReceiving(void);
bool RADIO_APP_isWatching(void);
// Settings changed the slot count: rebuild the watch group from scratch.
void RADIO_APP_SlotsChanged(void);
// A channel was renamed or deleted elsewhere: drop cached copies, and send
// slots whose channel is gone back to VFO.
void RADIO_APP_ChannelsChanged(void);
bool RADIO_APP_ApplyRxSettings(void);
void RADIO_APP_init();
void RADIO_APP_deinit(void);
void RADIO_APP_cancelShortcut(void);
void RADIO_APP_PttRelease(void); // High priority, even under keyboard lock.
void RADIO_APP_TxSafetyUpdate(void); // Run in the system loop, independent of UI.
bool RADIO_APP_isShortcutPending(void);
void RADIO_APP_update();
bool RADIO_APP_key(KEY_Code_t key, Key_State_t state);
void RADIO_APP_render();

#endif /* end of include guard: RADIO_APP_H */
