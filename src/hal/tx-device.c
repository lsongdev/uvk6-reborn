#include "hal/tx-device.h"

// Physical TX is intentionally unavailable in production firmware.
// Never invoke BK4819_PrepareTransmit, EnableTXLink, or drive PA GPIO from
// this stub. Hardware support must be a separately reviewed implementation.
TxCapabilities TX_DEVICE_Capabilities(void) {
  return (TxCapabilities){0};
}
TxPolicy TX_DEVICE_Policy(void) {
  return (TxPolicy){0};
}
bool TX_DEVICE_Prepare(const TxPlan *plan) {
  (void)plan;
  return false;
}
bool TX_DEVICE_Enable(void) {
  return false;
}
void TX_DEVICE_Disable(void) {}
