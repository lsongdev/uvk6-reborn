#ifndef RX_DEVICE_H
#define RX_DEVICE_H

#include "radio-model.h"

// Apply an already validated receive plan once. This is the sole hardware
// tuning path; callers own the user state and failed-plan recovery.
bool RX_DEVICE_Apply(const RxPlan *plan, RxDevice previous);

#endif
