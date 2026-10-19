#ifndef TX_DEVICE_H
#define TX_DEVICE_H
#include "core/tx-model.h"

// Fixed UV-K6 board: NFM TX with factory EEPROM power calibration.
TxCapabilities TX_DEVICE_Capabilities(uint32_t frequency_hz);
TxPolicy TX_DEVICE_Policy(void);
bool TX_DEVICE_Prepare(const TxPlan *plan);
bool TX_DEVICE_Enable(void);
void TX_DEVICE_Disable(void);
bool TX_DEVICE_PttPressed(void);
bool TX_DEVICE_VoltageValid(void);
#endif
