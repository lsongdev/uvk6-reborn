#include "hal/scheduler.h"
#include "hal/systick.h"
#include "drivers/dp32g030/timer.h"

uint32_t Now(void) { return GetUptimeMs(); }

void SetTimeout(uint32_t *v, uint32_t t) {
  *v = t == UINT32_MAX ? UINT32_MAX : Now() + t;
}

bool CheckTimeout(uint32_t *v) { return Now() >= *v; }
