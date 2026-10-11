#include "radio-model.h"
#include <stddef.h>

#define MHZ_HZ 1000000u
#define PRESET(mode_, step_, bw_, sq_) {mode_, step_, bw_, sq_, false}
const char *const rxModeNames[RX_MODE_COUNT] = {"NFM", "WFM", "AM", "LSB", "USB"};
// Narrower ranges win. Shortcut order is independent of range matching.
const RadioBand radioBands[] = {
    {"LW", 150000, 530000, 225000, true, PRESET(RX_AM, 1000, 6000, 0)},
    {"MW", 530000, 1800000, 999000, true, PRESET(RX_AM, 9000, 6000, 0)},
    {"SW", 1800000, 30000001, 7100000, true, PRESET(RX_AM, 5000, 6000, 0)},
    {"FM Radio", 64*MHZ_HZ, 108*MHZ_HZ+1, 95900000, true, PRESET(RX_WFM, 100000, 0, 0)},
    {"Airband", 118*MHZ_HZ, 137*MHZ_HZ, 125*MHZ_HZ, true, PRESET(RX_AM, 8330, 6000, 0)},
    {"2m", 144*MHZ_HZ, 148*MHZ_HZ, 145500000, true, PRESET(RX_NFM, 12500, 12000, 4)},
    {"70cm", 430*MHZ_HZ, 440*MHZ_HZ, 438500000, true, PRESET(RX_NFM, 12500, 12000, 4)},
    {"VHF", 30*MHZ_HZ, 300*MHZ_HZ, 151000000, false, PRESET(RX_NFM, 25000, 12000, 4)},
    {"UHF", 300*MHZ_HZ, RADIO_MAX_HZ+1, 445000000, false, PRESET(RX_NFM, 25000, 12000, 4)},
};
const uint8_t radioBandCount = sizeof(radioBands)/sizeof(radioBands[0]);

uint8_t RADIO_MODEL_BandAt(uint32_t frequency_hz) {
  uint8_t best = UINT8_MAX;
  uint32_t width = UINT32_MAX;
  for (uint8_t i = 0; i < radioBandCount; ++i) {
    const RadioBand *band = &radioBands[i];
    if (frequency_hz >= band->start_hz && frequency_hz < band->end_hz &&
        band->end_hz - band->start_hz < width) {
      best = i;
      width = band->end_hz - band->start_hz;
    }
  }
  return best;
}

uint8_t RADIO_MODEL_NextBand(uint8_t current) {
  for (uint8_t offset = 1; offset <= radioBandCount; ++offset) {
    uint8_t i = (current + offset) % radioBandCount;
    if (radioBands[i].shortcut)
      return i;
  }
  return current;
}

uint32_t RADIO_MODEL_Resolution(RxDevice device, RxMode mode) {
  if (device == RX_DEVICE_BK || mode == RX_LSB || mode == RX_USB)
    return 10;
  if (mode == RX_WFM)
    return device == RX_DEVICE_SI ? 10000 : 100000;
  return 1000;
}

static bool bandwidthSupported(RxDevice device, RxMode mode, uint32_t width) {
  static const uint16_t bk[] = {6000,7000,9000,10000,12000,14000,17000,20000,23000,26000};
  static const uint16_t am[] = {6000,4000,3000,2000,1000,1800,2500};
  static const uint16_t ssb[] = {1200,2200,3000,4000,500,1000};
  if (mode == RX_WFM)
    return width == 0;
  const uint16_t *values = device == RX_DEVICE_BK ? bk : (mode == RX_AM ? am : ssb);
  uint8_t count = device == RX_DEVICE_BK ? sizeof(bk)/sizeof(bk[0])
                                       : (mode == RX_AM ? sizeof(am)/sizeof(am[0]) : sizeof(ssb)/sizeof(ssb[0]));
  for (uint8_t i = 0; i < count; ++i)
    if (width == values[i])
      return true;
  return false;
}

RxResult RADIO_MODEL_Plan(uint32_t frequency_hz, RadioParameters parameters,
                          ReceiverCapabilities capabilities, RxPlan *plan) {
  if (!plan || (unsigned)parameters.mode >= RX_MODE_COUNT || !parameters.step_hz ||
      parameters.squelch > 10)
    return RX_INVALID_PARAMETERS;
  if (frequency_hz < RADIO_MIN_HZ || frequency_hz > RADIO_MAX_HZ)
    return RX_OUT_OF_RANGE;
  RxDevice device;
  switch (parameters.mode) {
  case RX_WFM:
    // Broadcast wide FM must not fall back to the BK narrowband receiver.
    if (frequency_hz < 64000000 || frequency_hz > 108000000)
      return RX_UNSUPPORTED_MODE;
    if (capabilities.si)
      device = RX_DEVICE_SI;
    else if (capabilities.broadcast)
      device = RX_DEVICE_BROADCAST;
    else
      return RX_UNSUPPORTED_MODE;
    break;
  case RX_LSB:
  case RX_USB:
    if (!capabilities.si || frequency_hz > 30000000)
      return RX_UNSUPPORTED_MODE;
    if (!capabilities.ssb_patch)
      return RX_PATCH_MISSING;
    device = RX_DEVICE_SI;
    break;
  case RX_AM:
    if (frequency_hz <= 30000000 && capabilities.si) {
      device = RX_DEVICE_SI;
      break;
    }
    // Fall through to BK where its existing AM implementation is available.
    // No BK pseudo-SSB fallback is exposed as a supported SSB receiver.
    if (frequency_hz < 15880000)
      return RX_UNSUPPORTED_MODE;
    device = RX_DEVICE_BK;
    break;
  case RX_NFM:
    // NFM always uses BK, including frequencies overlapping SI's AM or WFM.
    // RX capability alone never grants transmit permission.
    if (frequency_hz < 15880000)
      return RX_UNSUPPORTED_MODE;
    device = RX_DEVICE_BK;
    break;
  case RX_MODE_COUNT:
    return RX_INVALID_PARAMETERS;
  }
  uint32_t resolution = RADIO_MODEL_Resolution(device, parameters.mode);
  if (parameters.step_hz < resolution ||
      !bandwidthSupported(device, parameters.mode, parameters.bandwidth_hz))
    return RX_INVALID_PARAMETERS;
  uint32_t actual = ((frequency_hz + resolution/2) / resolution) * resolution;
  if (actual < RADIO_MIN_HZ || actual > RADIO_MAX_HZ)
    return RX_OUT_OF_RANGE;
  *plan = (RxPlan){device, actual, parameters};
  return RX_OK;
}

uint32_t RADIO_MODEL_Step(uint32_t frequency_hz, uint32_t step_hz, bool up) {
  if (frequency_hz < RADIO_MIN_HZ || frequency_hz > RADIO_MAX_HZ)
    return frequency_hz;
  if (up)
    return step_hz > RADIO_MAX_HZ-frequency_hz ? RADIO_MAX_HZ : frequency_hz+step_hz;
  return step_hz > frequency_hz-RADIO_MIN_HZ ? RADIO_MIN_HZ : frequency_hz-step_hz;
}

bool RADIO_MODEL_CanTransmit(const RxPlan *plan) {
  // Board TX limits are not independently verified: default to RX only.
  // A band flag alone cannot enable the PA; add a validated board TX table
  // before exposing a transmit-enable control.
  (void)plan;
  return false;
}
