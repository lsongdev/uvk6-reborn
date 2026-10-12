#include "../src/core/radio-model.h"
#include <assert.h>
#include <string.h>

int main(void) {
  ReceiverCapabilities si = {true, false, true};
  ReceiverCapabilities bk1080 = {false, true, false};
  ReceiverCapabilities none = {false, false, false};
  RxPlan plan;
  RadioParameters parameters = radioBands[RADIO_MODEL_BandAt(95925000)].preset;
  assert(parameters.mode == RX_WFM);
  assert(RADIO_MODEL_Plan(95925000, parameters, si, &plan) == RX_OK);
  assert(plan.device == RX_DEVICE_SI && plan.frequency_hz == 95930000);
  assert(RADIO_MODEL_Plan(95925000, parameters, bk1080, &plan) == RX_OK);
  assert(plan.device == RX_DEVICE_BROADCAST && plan.frequency_hz == 95900000);
  assert(RADIO_MODEL_Plan(95925000, parameters, none, &plan) == RX_UNSUPPORTED_MODE);

  // The selected demodulation mode determines the receiver at an overlap.
  RadioParameters nfm = {RX_NFM, 12500, 12000, 0};
  RadioParameters wfm = {RX_WFM, 100000, 0, 0};
  assert(strcmp(rxModeNames[RX_NFM], "NFM") == 0);
  assert(RADIO_MODEL_Plan(100000000, nfm, si, &plan) == RX_OK);
  assert(plan.device == RX_DEVICE_BK && plan.frequency_hz == 100000000);
  assert(!RADIO_MODEL_CanTransmit(&plan)); // BK RX is not TX authorization.
  assert(RADIO_MODEL_Plan(100000000, wfm, si, &plan) == RX_OK);
  assert(plan.device == RX_DEVICE_SI && !RADIO_MODEL_CanTransmit(&plan));
  assert(RADIO_MODEL_Plan(100000000, wfm, bk1080, &plan) == RX_OK);
  assert(plan.device == RX_DEVICE_BROADCAST && !RADIO_MODEL_CanTransmit(&plan));
  assert(RADIO_MODEL_Plan(100000000, wfm, none, &plan) == RX_UNSUPPORTED_MODE);
  assert(RADIO_MODEL_Plan(100000000, nfm, none, &plan) == RX_OK);
  assert(plan.device == RX_DEVICE_BK);

  // In the 18-26.1 MHz overlap, SI supports AM/SSB, not broadcast WFM.
  assert(RADIO_MODEL_Plan(20000000, nfm, si, &plan) == RX_OK);
  assert(plan.device == RX_DEVICE_BK && !RADIO_MODEL_CanTransmit(&plan));
  assert(RADIO_MODEL_Plan(20000000, wfm, si, &plan) == RX_UNSUPPORTED_MODE);
  assert(RADIO_MODEL_Plan(145500000, wfm, si, &plan) == RX_UNSUPPORTED_MODE);

  parameters = radioBands[RADIO_MODEL_BandAt(438500000)].preset;
  assert(RADIO_MODEL_Plan(438500000, parameters, si, &plan) == RX_OK);
  assert(plan.device == RX_DEVICE_BK && plan.frequency_hz == 438500000);
  assert(!RADIO_MODEL_CanTransmit(&plan));
  parameters.squelch = 11;
  assert(RADIO_MODEL_Plan(438500000, parameters, si, &plan) == RX_INVALID_PARAMETERS);
  assert(!RADIO_MODEL_CanTransmit(&plan)); // RX planning never grants TX.

  parameters = radioBands[RADIO_MODEL_BandAt(20000000)].preset;
  assert(parameters.mode == RX_AM);
  assert(RADIO_MODEL_Plan(20000000, parameters, si, &plan) == RX_OK);
  assert(plan.device == RX_DEVICE_SI);
  assert(RADIO_MODEL_Plan(20000000, parameters, none, &plan) == RX_OK);
  assert(plan.device == RX_DEVICE_BK);
  assert(RADIO_MODEL_Plan(7100000, parameters, none, &plan) == RX_UNSUPPORTED_MODE);
  parameters.mode = RX_LSB;
  parameters.bandwidth_hz = 3000;
  assert(RADIO_MODEL_Plan(7100050, parameters, si, &plan) == RX_OK);
  assert(plan.frequency_hz == 7100050);
  si.ssb_patch = false;
  assert(RADIO_MODEL_Plan(7100050, parameters, si, &plan) == RX_PATCH_MISSING);
  assert(RADIO_MODEL_Plan(438500000, parameters, si, &plan) == RX_UNSUPPORTED_MODE);
  parameters.mode = (RxMode)-1;
  assert(RADIO_MODEL_Plan(7100000, parameters, si, &plan) == RX_INVALID_PARAMETERS);
  parameters.mode = RX_AM;
  parameters.bandwidth_hz = 1234;
  assert(RADIO_MODEL_Plan(7100000, parameters, si, &plan) == RX_INVALID_PARAMETERS);
  parameters.bandwidth_hz = 6000;
  parameters.step_hz = 10;
  assert(RADIO_MODEL_Plan(7100000, parameters, si, &plan) == RX_INVALID_PARAMETERS);
  parameters.step_hz = 1000;
  assert(RADIO_MODEL_Plan(RADIO_MIN_HZ-1, parameters, si, &plan) == RX_OUT_OF_RANGE);
  assert(RADIO_MODEL_Plan(RADIO_MAX_HZ+1, parameters, si, &plan) == RX_OUT_OF_RANGE);

  assert(RADIO_MODEL_BandAt(RADIO_MIN_HZ-1) == UINT8_MAX);
  assert(RADIO_MODEL_BandAt(RADIO_MAX_HZ+1) == UINT8_MAX);
  assert(strcmp(radioBands[RADIO_MODEL_BandAt(147999990)].name, "2m") == 0);
  assert(strcmp(radioBands[RADIO_MODEL_BandAt(148000000)].name, "VHF") == 0);
  assert(strcmp(radioBands[RADIO_MODEL_BandAt(439999990)].name, "70cm") == 0);
  assert(strcmp(radioBands[RADIO_MODEL_BandAt(440000000)].name, "UHF") == 0);
  assert(RADIO_MODEL_NextBand(6) == 0);
  assert(RADIO_MODEL_Step(107950000, 100000, true) == 108050000);
  assert(RADIO_MODEL_Step(RADIO_MAX_HZ, UINT32_MAX, true) == RADIO_MAX_HZ);
  assert(RADIO_MODEL_Step(RADIO_MIN_HZ, UINT32_MAX, false) == RADIO_MIN_HZ);
  assert(RADIO_MODEL_Step(438500000, 12500, false) == 438487500);
  assert(RADIO_MODEL_Step(118000000, 8330, true) == 118008330);
  assert(RADIO_MODEL_Step(118008330, 8330, true) == 118016670);
  assert(RADIO_MODEL_Step(118016670, 8330, true) == 118025000);
  assert(RADIO_MODEL_Step(118025000, 8330, false) == 118016670);
  assert(RADIO_MODEL_Step(118000000, 8330, false) == 118000000);
  assert(RADIO_MODEL_Plan(700000000, nfm, si, &plan) == RX_UNSUPPORTED_MODE);
  assert(RADIO_MODEL_Plan(1320000000u, nfm, si, &plan) == RX_UNSUPPORTED_MODE);
  assert(RADIO_MODEL_Plan(920000000, nfm, si, &plan) == RX_OK);
  assert(RADIO_MODEL_Plan(17000000, nfm, si, &plan) == RX_UNSUPPORTED_MODE);
  return 0;
}
