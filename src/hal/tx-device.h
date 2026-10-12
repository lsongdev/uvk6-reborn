#ifndef TX_DEVICE_H
#define TX_DEVICE_H
#include "core/tx-model.h"

// Fail closed until board PA/filter/calibration facts are verified in hardware.
// These declarations are retained so runtime tests can inject a fake HAL.
TxCapabilities TX_DEVICE_Capabilities(void);
TxPolicy TX_DEVICE_Policy(void);
bool TX_DEVICE_Prepare(const TxPlan *plan);
bool TX_DEVICE_Enable(void);
void TX_DEVICE_Disable(void);
#endif
