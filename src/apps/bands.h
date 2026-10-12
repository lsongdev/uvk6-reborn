#ifndef APP_BANDS_H
#define APP_BANDS_H
#include "hal/keyboard.h"
void BANDS_init(void);
void BANDS_render(void);
bool BANDS_key(KEY_Code_t key, Key_State_t state);
#endif
