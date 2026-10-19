#include "hal/scheduler.h"
#include "hal/systick.h"
#include "drivers/mcu/dp32g030/timer.h"

uint32_t Now(void) { return GetUptimeMs(); }


