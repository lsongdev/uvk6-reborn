#include "core/tx-model.h"

const char *const txPowerNames[TX_POWER_HIGH + 1] = {"Low", "Medium", "High"};

bool TX_MODEL_ConfigValid(const TxConfig *c) {
  return c && (unsigned)c->power <= TX_POWER_HIGH && (unsigned)c->shift < TX_SHIFT_COUNT &&
         TONE_Valid(c->tone) && !(c->offset_hz % 10u) && c->offset_hz <= TX_OFFSET_MAX_HZ &&
         TX_MODEL_FrequencyValid(c->split_hz);
}
uint32_t TX_MODEL_Frequency(const TxConfig *c, uint32_t rx) {
  switch (c->shift) {
  case TX_SHIFT_PLUS: return rx + c->offset_hz;
  case TX_SHIFT_MINUS: return c->offset_hz < rx ? rx - c->offset_hz : 0;
  case TX_SHIFT_SPLIT: return c->split_hz ? c->split_hz : rx;
  default: return rx;
  }
}

TxResult TX_MODEL_Plan(const RxPlan *rx, const TxConfig *config,
                       const TxCapabilities *capabilities,
                       const TxPolicy *policy, TxPlan *out) {
  // Failure must never leave a previously authorized plan in the output.
  if (out) *out = (TxPlan){0};
  if (!rx || !config || !capabilities || !policy || !out)
    return TX_HW_FAULT;
  if (!TX_MODEL_ConfigValid(config))
    return TX_HW_FAULT;
  uint32_t frequency = TX_MODEL_Frequency(config, rx->frequency_hz);
  if (!config->enabled)
    return TX_DISABLED;
  if (rx->parameters.mode != RX_NFM || rx->device != RX_DEVICE_BK)
    return TX_UNSUPPORTED_MODE;
  if (!TONE_Valid(rx->parameters.rx_tone) || !rx->parameters.step_hz || rx->parameters.squelch > 10 ||
      rx->frequency_hz % 10u)
    return TX_HW_FAULT;
  // TX must never expand outside the RF chip's already-planned RX range,
  // regardless of what a misconfigured board policy requests.
  if (!((rx->frequency_hz >= 18000000u && rx->frequency_hz <= 660000000u) ||
        (rx->frequency_hz >= 840000000u && rx->frequency_hz <= 1300000000u)))
    return TX_OUT_OF_BAND;
  if (!frequency || !TX_MODEL_FrequencyValid(frequency)) return TX_OUT_OF_BAND;
  if (!capabilities->available || !policy->authorized)
    return TX_UNVERIFIED_HARDWARE;
  if (!policy->count || policy->count > TX_RANGE_CAPACITY) return TX_OUT_OF_BAND;
  bool allowed = false;
  for (uint8_t i=0; i<policy->count; ++i) {
    const TxFrequencyRange *range = &policy->ranges[i];
    if (range->lower_hz >= range->upper_hz) return TX_OUT_OF_BAND;
    allowed |= frequency >= range->lower_hz && frequency < range->upper_hz;
  }
  if (!allowed) return TX_OUT_OF_BAND;
  if (!(capabilities->power_mask & (1u << config->power))) return TX_UNCALIBRATED;
  *out = (TxPlan){.frequency_hz = frequency, .power = config->power, .tone = config->tone, .tail = config->tail && config->tone != 0};
  return TX_OK;
}
