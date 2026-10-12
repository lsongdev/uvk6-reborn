#include "core/radio-model.h"
#include <stddef.h>

#define MHZ_HZ 1000000u
#define PRESET(mode_, step_, bw_, sq_) {mode_, step_, bw_, sq_}
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
    if (!((frequency_hz >= 18000000u && frequency_hz <= 660000000u) ||
          (frequency_hz >= 840000000u && frequency_hz <= 1300000000u)))
      return RX_UNSUPPORTED_MODE;
    device = RX_DEVICE_BK;
    break;
  case RX_NFM:
    // NFM always uses BK, including frequencies overlapping SI's AM or WFM.
    // RX capability alone never grants transmit permission.
    if (!((frequency_hz >= 18000000u && frequency_hz <= 660000000u) ||
          (frequency_hz >= 840000000u && frequency_hz <= 1300000000u)))
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
  // Airband's 8.33... kHz spacing is exactly 25 kHz / 3, not 8,330 Hz.
  // Store the legacy 8330 menu choice but use a 25 kHz / 3 channel raster
  // relative to 118 MHz, rounded to the BK4819's 10 Hz resolution.
  if (step_hz == 8330u && frequency_hz >= 118000000u &&
      frequency_hz < 137000000u) {
    uint32_t channel = ((frequency_hz - 118000000u)*3u + 12500u)/25000u;
    if (!up && channel == 0)
      return frequency_hz;
    if (up && channel >= 2279u)
      return frequency_hz;
    channel = up ? channel+1u : channel-1u;
    uint32_t offset = (channel*25000u + 1u)/3u;
    return 118000000u + ((offset+5u)/10u)*10u;
  }
  if (up)
    return step_hz > RADIO_MAX_HZ-frequency_hz ? RADIO_MAX_HZ : frequency_hz+step_hz;
  return step_hz > frequency_hz-RADIO_MIN_HZ ? RADIO_MIN_HZ : frequency_hz-step_hz;
}

// Shared option filtering for live controls and the band preset editor.
bool RADIO_MODEL_Adjust(uint32_t frequency, RadioParameters *parameters,
                        ReceiverCapabilities capabilities, RadioParameterField field, bool up) {
  if (!parameters || (unsigned)parameters->mode >= RX_MODE_COUNT)
    return false;
  RadioParameters candidate = *parameters;
  RxPlan plan;
  if (field == RADIO_FIELD_MODE) {
    for (uint8_t offset = 1; offset < RX_MODE_COUNT; ++offset) {
      candidate = *parameters;
      candidate.mode = (parameters->mode + (up ? offset : RX_MODE_COUNT-offset))%RX_MODE_COUNT;
      uint32_t minimum = candidate.mode == RX_WFM ? (capabilities.si ? 10000 : 100000)
                       : candidate.mode == RX_AM && frequency <= 30000000 && capabilities.si ? 1000 : 10;
      if (candidate.step_hz < minimum) candidate.step_hz = minimum;
      if (candidate.mode == RX_WFM) candidate.bandwidth_hz = 0;
      RxResult result = RADIO_MODEL_Plan(frequency, candidate, capabilities, &plan);
      if (result == RX_INVALID_PARAMETERS) {
        candidate.bandwidth_hz = candidate.mode == RX_WFM ? 0
          : candidate.mode == RX_LSB || candidate.mode == RX_USB ? 3000
          : candidate.mode == RX_AM ? 6000 : 12000;
        result = RADIO_MODEL_Plan(frequency, candidate, capabilities, &plan);
      }
      if (result == RX_OK) { *parameters = candidate; return true; }
    }
    return false;
  }
  if (field == RADIO_FIELD_SQUELCH) {
    candidate.squelch = (candidate.squelch + (up ? 1 : 10))%11;
    *parameters = candidate;
    return true;
  }
  static const uint32_t steps[] = {20,50,500,1000,2500,5000,6250,8330,9000,10000,12500,25000,50000,100000,500000};
  static const uint32_t widths[] = {500,1000,1200,1800,2000,2200,2500,3000,4000,6000,7000,9000,10000,12000,14000,17000,20000,23000,26000};
  if (field != RADIO_FIELD_STEP && field != RADIO_FIELD_BANDWIDTH)
    return false;
  if (field == RADIO_FIELD_BANDWIDTH && candidate.mode == RX_WFM)
    return false;
  const uint32_t *values = field == RADIO_FIELD_STEP ? steps : widths;
  uint8_t count = field == RADIO_FIELD_STEP ? sizeof(steps)/sizeof(steps[0]) : sizeof(widths)/sizeof(widths[0]);
  uint32_t current = field == RADIO_FIELD_STEP ? candidate.step_hz : candidate.bandwidth_hz;
  uint8_t index = 0;
  for (uint8_t i = 0; i < count; ++i) if (values[i] == current) index = i;
  for (uint8_t offset = 1; offset < count; ++offset) {
    uint8_t i = (index + (up ? offset : count-offset))%count;
    if (field == RADIO_FIELD_STEP) candidate.step_hz = values[i];
    else candidate.bandwidth_hz = values[i];
    if (RADIO_MODEL_Plan(frequency, candidate, capabilities, &plan) == RX_OK) {
      *parameters = candidate; return true;
    }
  }
  return false;
}
