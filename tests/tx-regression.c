#include "core/tx-model.h"
#include "core/tx-runtime.h"
#include <assert.h>

static unsigned muted, prepared, enabled, disabled, restored;
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
  RxPlan rx = {RX_DEVICE_BK,145500000,{RX_NFM,12500,12000,4,false}};
  TxConfig cfg = {true,TX_POWER_LOW};
  TxCapabilities hw = {true,true};
  TxPolicy policy = {true,144000000,148000000};
  TxPlan plan = {0};
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OK);
  assert(plan.frequency_hz==145500000);
  cfg.enabled=false;assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_DISABLED);
  cfg.enabled=true;
  cfg.power=TX_POWER_HIGH;assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_UNCALIBRATED);
  cfg.power=TX_POWER_LOW;
  rx.parameters.mode=RX_WFM;assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_UNSUPPORTED_MODE);
  rx.parameters.mode=RX_NFM;
  rx.device=RX_DEVICE_SI;assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_UNSUPPORTED_MODE);
  rx.device=RX_DEVICE_BK;
  policy.authorized=false;assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_UNVERIFIED_HARDWARE);
  policy.authorized=true;hw.verified=false;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_UNVERIFIED_HARDWARE);
  hw.verified=true;
  policy.upper_hz=145500000;assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OUT_OF_BAND);
  policy.upper_hz=148000000;
  assert(TX_MODEL_Plan(&rx,&cfg,&hw,&policy,&plan)==TX_OK);

  TxOperations ops = {NULL,mute,prepare,enable,disable,restore};
  TxRuntime state = {0};
  assert(TX_RuntimePress(&state,&rx,&plan,2,1000,&ops));
  assert(state.mode==RADIO_TX && prepared==1 && enabled==1);
  assert(disabled==1 && muted==1);
  assert(!TX_RuntimePress(&state,&rx,&plan,2,1000,&ops));
  assert(!TX_RuntimeUpdate(&state,2999,&ops));
  assert(TX_RuntimeUpdate(&state,3000,&ops));
  assert(state.mode==RADIO_RX && state.wait_release && restored==1 && disabled==2);
  assert(!TX_RuntimePress(&state,&rx,&plan,2,3100,&ops));
  TX_RuntimeRelease(&state,&ops);
  assert(!state.wait_release);
  enable_ok=false;
  assert(!TX_RuntimePress(&state,&rx,&plan,2,4000,&ops));
  assert(state.mode==RADIO_RX && state.wait_release && disabled==5 && restored==2);
  TX_RuntimeRelease(&state,&ops);
  enable_ok=true;
  prepare_ok=false;
  assert(!TX_RuntimePress(&state,&rx,&plan,2,5000,&ops));
  TX_RuntimeRelease(&state,&ops);
  prepare_ok=true;
  assert(TX_RuntimePress(&state,&rx,&plan,2,6000,&ops));
  TX_RuntimeRelease(&state,&ops);
  assert(state.mode==RADIO_RX && !state.wait_release);
  restore_ok=false;
  assert(TX_RuntimePress(&state,&rx,&plan,2,9000,&ops));
  TX_RuntimeRelease(&state,&ops);
  assert(state.mode==RADIO_FAULT && !state.wait_release);
  assert(!TX_RuntimePress(&state,&rx,&plan,2,9100,&ops));
  return 0;
}
