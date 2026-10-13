#ifndef VFO_APP_H
#define VFO_APP_H

#include "hal/keyboard.h"
#include <stdbool.h>
#include <stdint.h>

bool VFO_isReceiving(void);
bool VFO_ApplyRxSettings(void);
void VFO_init();
void VFO_deinit(void);
void VFO_cancelShortcut(void);
void VFO_PttRelease(void); // High priority, even under keyboard lock.
void VFO_TxSafetyUpdate(void); // Run in the system loop, independent of UI.
bool VFO_isShortcutPending(void);
void VFO_update();
bool VFO_key(KEY_Code_t key, Key_State_t state);
void VFO_render();

#endif /* end of include guard: VFO_APP_H */
