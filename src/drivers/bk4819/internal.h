#ifndef BK4819_INTERNAL_H
#define BK4819_INTERNAL_H
#include "hal/board.h"
#include <stdbool.h>
static inline bool isBk4829(void) { return BOARD_RadioChip() == BOARD_RADIO_BK4829; }
void BK4819_BusInit(void);
#endif
