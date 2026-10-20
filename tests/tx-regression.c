#include "core/tx-model.h"
#include "radio/tx-runtime.h"
#include <assert.h>
#include <stddef.h>

static unsigned muted, prepared, enabled, disabled, restored, tails;
static bool tail_ok = true;
static bool tail(void *x) { (void)x; ++tails; return tail_ok; }
static bool enable_ok = true, restore_ok = true, prepare_ok = true;
static void mute(void *x) { (void)x; ++muted; }
static bool prepare(void *x, const TxPlan *plan) {
  (void)x; ++prepared;
  assert(plan->frequency_hz == 145500000);
  return prepare_ok;
}
static bool enable(void *x) { (void)x; ++enabled; return enable_ok; }
static void disable(void *x) { (void)x; ++disabled; }
static bool restore(void *x, const RxPlan *plan) {
  (void)x; ++restored; assert(plan->frequency_hz == 145500000);
  return restore_ok;
}
int main(void) {
  RxPlan rx = {RX_DEVICE_BK,145500000,{RX_NFM,12500,12000,4,0}};
  TxConfig cfg = {.enabled=true,.power=TX_POWER_LOW};
  TxCapabilities hw = {true,true};
  TxPolicy policy = {true,1,{{144000000,148000000}}};
  TxPlan plan = {0};
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OK);
  assert(plan.frequency_hz==145500000);
  cfg.enabled=false;
  plan.frequency_hz = 123456780u;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_DISABLED);
  assert(plan.frequency_hz == 0); // No stale TX authorization on rejection.
  cfg.enabled=true;
  cfg.power=TX_POWER_HIGH;assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_UNCALIBRATED);
  cfg.power=TX_POWER_LOW;
  rx.parameters.mode=RX_WFM;assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_UNSUPPORTED_MODE);
  rx.parameters.mode=RX_NFM;
  rx.device=RX_DEVICE_SI;assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_UNSUPPORTED_MODE);
  rx.device=RX_DEVICE_BK;
  policy.authorized=false;assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_UNVERIFIED_HARDWARE);
  policy.authorized=true;hw.available=false;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_UNVERIFIED_HARDWARE);
  hw.available=true;
  policy.ranges[0].upper_hz=145500000;assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OUT_OF_BAND);
  policy.ranges[0].upper_hz=148000000;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OK);
  cfg.power = (TxPower)255; // A corrupt level is an invalid configuration.
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_HW_FAULT);
  assert(plan.frequency_hz == 0);
  cfg.power = TX_POWER_LOW;
  rx.parameters.step_hz = 0;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_HW_FAULT);
  rx.parameters.step_hz = 12500;
  rx.frequency_hz = 700000000u;
  policy.ranges[0].lower_hz = 690000000u;
  policy.ranges[0].upper_hz = 710000000u;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OUT_OF_BAND);
  policy.ranges[0].lower_hz = 144000000u;
  policy.ranges[0].upper_hz = 148000000u;
  rx.frequency_hz = 145500000u;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OK);

  // Split: an absolute TX frequency, never changing RX.
  cfg.shift=TX_SHIFT_SPLIT;cfg.split_hz=146500000;cfg.tone=155;cfg.tail=true;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OK);
  assert(plan.frequency_hz==146500000 && plan.tone==155 && plan.tail);
  assert(rx.frequency_hz==145500000);
  cfg.split_hz=438500000;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OUT_OF_BAND);
  assert(plan.frequency_hz==0);
  cfg.split_hz=700000000;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_HW_FAULT);
  cfg.split_hz=146500001;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_HW_FAULT);
  cfg.split_hz=0; // Unset split transmits on RX.
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OK && plan.frequency_hz==145500000);
  // Repeater shift: +/- offset from RX, the offset kept across shifts.
  cfg.shift=TX_SHIFT_PLUS;cfg.offset_hz=600000;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OK && plan.frequency_hz==146100000);
  cfg.shift=TX_SHIFT_MINUS;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OK && plan.frequency_hz==144900000);
  cfg.offset_hz=200000000; // Beyond the limit, and below 0 Hz for MINUS.
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_HW_FAULT);
  assert(!TX_MODEL_Frequency(&cfg,145500000));
  cfg.offset_hz=600005;assert(!TX_MODEL_ConfigValid(&cfg));
  cfg.offset_hz=600000;cfg.shift=TX_SHIFT_COUNT;assert(!TX_MODEL_ConfigValid(&cfg));
  cfg.shift=TX_SHIFT_OFF;cfg.tone=259;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_HW_FAULT);
  cfg.tone=0;assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OK && plan.frequency_hz==145500000 && !plan.tail);

  // Each calibrated level and each disjoint board range is checked independently.
  hw.power_mask=7;
  policy.count=2;policy.ranges[1]=(TxFrequencyRange){400000000u,470000000u};
  cfg.shift=TX_SHIFT_SPLIT;cfg.split_hz=438500000;cfg.power=TX_POWER_HIGH;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OK && plan.power==TX_POWER_HIGH);
  cfg.power=TX_POWER_MEDIUM;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OK && plan.power==TX_POWER_MEDIUM);
  cfg.split_hz=300000000;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OUT_OF_BAND);
  cfg.split_hz=470000000;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OUT_OF_BAND);
  cfg.shift=TX_SHIFT_OFF;cfg.power=TX_POWER_LOW;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OK);

  TxOperations ops = {.mute_rx=mute,.prepare=prepare,.enable=enable,
    .disable=disable,.restore_rx=restore,.begin_tail=tail};
  TxRuntime state = {0};
  assert(TX_RuntimePress(&state,&rx,&plan,2,1000,&ops));
  assert(state.mode==RADIO_TX && prepared==1 && enabled==1);
  assert(disabled==1 && muted==1);
  assert(!TX_RuntimePress(&state,&rx,&plan,2,1000,&ops));
  assert(!TX_RuntimeUpdate(&state,2999,&ops));
  assert(TX_RuntimeUpdate(&state,3000,&ops));
  assert(state.mode==RADIO_RX && state.wait_release && restored==1 && disabled==2);
  assert(!TX_RuntimePress(&state,&rx,&plan,2,3100,&ops));
  TX_RuntimeRelease(&state,state.started_at+1u,&ops);
  assert(!state.wait_release);
  enable_ok=false;
  assert(!TX_RuntimePress(&state,&rx,&plan,2,4000,&ops));
  assert(state.mode==RADIO_RX && state.wait_release && disabled==5 && restored==2);
  TX_RuntimeRelease(&state,state.started_at+1u,&ops);
  enable_ok=true;
  prepare_ok=false;
  assert(!TX_RuntimePress(&state,&rx,&plan,2,5000,&ops));
  TX_RuntimeRelease(&state,state.started_at+1u,&ops);
  prepare_ok=true;
  assert(TX_RuntimePress(&state,&rx,&plan,2,6000,&ops));
  TX_RuntimeRelease(&state,state.started_at+1u,&ops);
  assert(state.mode==RADIO_RX && !state.wait_release);
  restore_ok=false;
  assert(TX_RuntimePress(&state,&rx,&plan,2,9000,&ops));
  TX_RuntimeRelease(&state,state.started_at+1u,&ops);
  assert(state.mode==RADIO_FAULT && !state.wait_release);
  assert(!TX_RuntimePress(&state,&rx,&plan,2,9100,&ops));
  // A tail is a bounded RF phase, not a blocking sleep or a second TX.
  restore_ok=true;state=(TxRuntime){0};plan.tone=1;plan.tail=true;
  assert(TX_RuntimePress(&state,&rx,&plan,2,10000,&ops));
  unsigned off=disabled, back=restored;
  TX_RuntimeRelease(&state,10100,&ops);
  assert(state.mode==RADIO_TX_TAIL && tails==1 && disabled==off && restored==back);
  assert(!TX_RuntimePress(&state,&rx,&plan,2,10101,&ops));
  TX_RuntimeRelease(&state,10250,&ops);
  assert(state.tail_started_at==10100 && tails==1 && disabled==off);
  assert(TX_RuntimeUpdate(&state,10299,&ops)==TX_EVENT_NONE);
  assert(TX_RuntimeUpdate(&state,10300,&ops)==TX_EVENT_ENDED);
  assert(state.mode==RADIO_RX && disabled==off+1 && restored==back+1);
  assert(TX_RuntimeUpdate(&state,10400,&ops)==TX_EVENT_NONE);
  // Off, no tone, or failure to start the tail all restore RX immediately.
  for (unsigned scenario=0;scenario<3;++scenario) {
    plan.tail=scenario!=0;plan.tone=scenario==1 ? 0 : 155;tail_ok=scenario!=2;
    assert(TX_RuntimePress(&state,&rx,&plan,2,11000,&ops));
    TX_RuntimeRelease(&state,11100,&ops);
    assert(state.mode==RADIO_RX);
  }
  tail_ok=true;plan.tail=true;plan.tone=155;
  // TOT wins over the tail deadline; abort never emits a tail.
  assert(TX_RuntimePress(&state,&rx,&plan,1,20000,&ops));
  TX_RuntimeRelease(&state,20950,&ops);assert(state.mode==RADIO_TX_TAIL);
  assert(TX_RuntimeUpdate(&state,21000,&ops)==TX_EVENT_TIMEOUT);
  assert(state.mode==RADIO_RX && state.wait_release);
  TX_RuntimeRelease(&state,21001,&ops);
  assert(TX_RuntimePress(&state,&rx,&plan,1,22000,&ops));
  unsigned beforeTail=tails;
  TX_RuntimeRelease(&state,23000,&ops);
  assert(state.mode==RADIO_RX && state.wait_release && tails==beforeTail);
  TX_RuntimeRelease(&state,23001,&ops);
  assert(TX_RuntimePress(&state,&rx,&plan,2,24000,&ops));
  TX_RuntimeAbort(&state,&ops);
  assert(state.mode==RADIO_RX && state.wait_release && tails==beforeTail);
  TX_RuntimeRelease(&state,24001,&ops);
  assert(TX_RuntimePress(&state,&rx,&plan,2,25000,&ops));
  TX_RuntimeRelease(&state,25100,&ops);assert(state.mode==RADIO_TX_TAIL);
  TX_RuntimeAbort(&state,&ops);assert(state.mode==RADIO_RX && state.wait_release);
  TX_RuntimeRelease(&state,25101,&ops);
  // Unsigned deadlines remain correct across uptime wraparound.
  assert(TX_RuntimePress(&state,&rx,&plan,2,UINT32_MAX-100u,&ops));
  TX_RuntimeRelease(&state,UINT32_MAX-50u,&ops);
  assert(TX_RuntimeUpdate(&state,148,&ops)==TX_EVENT_NONE);
  assert(TX_RuntimeUpdate(&state,149,&ops)==TX_EVENT_ENDED);
  return 0;
}
