#ifndef RADIO_APP_H
#define RADIO_APP_H

#include "hal/keyboard.h"
#include <stdbool.h>
#include <stdint.h>

bool RADIO_APP_isReceiving(void);
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
