#ifndef VFO1_APP_H
#define VFO1_APP_H

#include "hal/keyboard.h"
#include <stdbool.h>
#include <stdint.h>

bool VFO1_isReceiving(void);
void VFO1_init();
void VFO1_deinit(void);
void VFO1_cancelShortcut(void);
void VFO1_PttRelease(void); // High priority, even under keyboard lock.
void VFO1_TxSafetyUpdate(void); // Run in the system loop, independent of UI.
void VFO1_update();
bool VFO1_key(KEY_Code_t key, Key_State_t state);
void VFO1_render();

#endif /* end of include guard: VFO1_APP_H */
