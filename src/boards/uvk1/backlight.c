#include "hal/board.h"
#include "boards/uvk1/pins.h"
void BOARD_BacklightInit(void) { GPIOF->BRR = 1u << UVK1_BACKLIGHT; }
void BOARD_BacklightSetLevel(uint8_t level) {
  GPIOF->BSRR = 1u << (UVK1_BACKLIGHT + (level ? 0u : 16u));
}
