#include "core/tx-model.h"

TxResult TX_MODEL_Plan(const RxPlan *rx, const TxConfig *config,
                       const TxCapabilities *capabilities,
                       const TxPolicy *policy, TxPlan *out) {
  if (!rx || !config || !capabilities || !policy || !out)
    return TX_HW_FAULT;
  if (!config->enabled)
    return TX_DISABLED;
  if (rx->parameters.mode != RX_NFM || rx->device != RX_DEVICE_BK)
    return TX_UNSUPPORTED_MODE;
  if (!capabilities->verified || !policy->authorized)
    return TX_UNVERIFIED_HARDWARE;
  if (rx->frequency_hz < policy->lower_hz ||
      rx->frequency_hz >= policy->upper_hz ||
      policy->lower_hz >= policy->upper_hz)
    return TX_OUT_OF_BAND;
  // Until medium/high power curves are measured, do not issue a TxPlan for
  // an uncalibrated output level.
  if (!capabilities->low_power_calibrated || config->power != TX_POWER_LOW)
    return TX_UNCALIBRATED;
  *out = (TxPlan){.frequency_hz = rx->frequency_hz, .power = TX_POWER_LOW};
  return TX_OK;
}
