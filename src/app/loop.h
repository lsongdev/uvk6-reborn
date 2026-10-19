#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

// Boot (configuration, radio, first screen) and the main loop; never returns.
void SYS_Main(void);

// Main-loop health for the PC link's diagnostics command.
typedef struct {
  uint32_t loops;          // Iterations since boot.
  uint16_t loopMaxMs;      // Longest iteration since the last take.
  uint16_t keyEvents;      // Key messages since boot.
  uint16_t squelchChanges; // Receiver open/close transitions since boot.
  uint8_t lastKey, lastKeyState;
} LoopStats;
void LOOP_TakeStats(LoopStats *out); // Resets loopMaxMs.

#endif
