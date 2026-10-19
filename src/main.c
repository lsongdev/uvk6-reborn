#include "hal/board.h"
#include "hal/storage.h"
#include "system.h"
#include "core/device-config.h"

void Main(void) {
  // A BSS-backed configuration must be initialized before early battery reads.
  CONFIG_Default(&gDeviceConfig);
  BOARD_Init();
  STORAGE_Init();

  SYS_Main();
}
