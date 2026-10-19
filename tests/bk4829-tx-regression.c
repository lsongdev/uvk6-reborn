// Real K1 PA mapping, TX device, BK4829 TX/RX/CSS register drivers.
// Only storage, physical PTT/battery and the register bus are modeled.
#define UVK6_ENABLE_TX 1
#include "../src/devices/tx-device.c"
#include <assert.h>
#include <string.h>

DeviceConfig gDeviceConfig = {.preferences=CONFIG_DEFAULT_PREFERENCES};
uint16_t gBatteryVoltage=760;
static uint16_t regs[128];
static struct { uint8_t reg; uint16_t value; } writes[1024];
static unsigned writeCount, delayCount, starts;
static bool pressed=true, readOK=true, stopOK=true, stopped;
static bool releaseDuringDelay, voltageDropDuringDelay;
static uint32_t lastOffset;
static uint8_t vhf[9]={95,95,95,105,105,105,145,150,145};
static uint8_t uhf[9]={90,90,90,100,100,100,145,150,145};

bool GPIO_Read(GpioLine line) { assert(line==GPIO_PTT); return !pressed; }
bool STORAGE_Read(StorageArea area, uint32_t offset, void *data, uint16_t size) {
  assert(area==STORAGE_CALIBRATION && size==9 && (offset==0xF0 || offset==0x120));
  lastOffset=offset;
  if (!readOK) return false;
  memcpy(data,offset==0xF0 ? vhf : uhf,size);
  return true;
}
bool RX_DEVICE_StopAll(void) { stopped=stopOK; return stopOK; }
void RX_DEVICE_InvalidateBk(void) { BK48X9_InvalidateRxMode(); }
void AUDIO_ToggleSpeaker(bool on) { assert(!on); }
void BK48X9_BusInit(void) {}
uint16_t BK48X9_ReadRegister(BK48X9_REGISTER_t reg) { return reg==0x0C ? 0 : regs[reg]; }
void BK48X9_WriteRegister(BK48X9_REGISTER_t reg, uint16_t value) {
  // Once PA is on, only independent LED changes or PA gate off are allowed.
  bool on = regs[0x33] & 0x20;
  if (on) assert(reg==0x33 && (!(value & 0x20) ||
                 ((value ^ regs[0x33]) & ~3u)==0));
  if (reg==0x33 && (value & 0x20) && !on) {
    assert(stopped && pressed && TX_DEVICE_VoltageValid());
    assert(!(value & 0x40) && regs[0x30]==0xC1FE && regs[0x37]==0x9D1F);
    assert(regs[0x50]==0x3B18 && regs[0x43]==0x4048 && delayCount);
    assert((regs[0x36] >> 8)==paBias && (regs[0x36] & 0x80));
    ++starts;
  }
  assert(writeCount < 1024);
  writes[writeCount].reg=reg; writes[writeCount++].value=value;
  regs[reg]=value;
}
uint32_t PREFERENCES_FilterBound(void) { return PREFERENCES_FILTER_BOUND_240; }
void SYS_DelayMs(uint32_t ms) {
  assert(ms==10 && !(regs[0x33] & 0x20) && !regs[0x36]);
  ++delayCount;
  if (releaseDuringDelay) pressed=false;
  if (voltageDropDuringDelay) gBatteryVoltage=599;
}
static void resetTrace(void) { writeCount=delayCount=0; stopped=false; }
static void checkOff(void) {
  assert(!(regs[0x33] & 0x21) && !regs[0x36] && !prepared);
}
static void restoreRx(uint32_t hz) {
  BK48X9_RX_TurnOn();
  BK48X9_SetRxMode(false);
  BK48X9_SetFilterBandwidth(BK48X9_FILTER_BW_12k);
  BK48X9_ToggleGpioOut(BK48X9_GPIO0_PIN28_RX_ENABLE,true);
  BK48X9_SelectFilter(hz/10u,PREFERENCES_FilterBound());
  BK48X9_TuneTo(hz/10u,true);
  assert(regs[0x30]==0xBFF1 && regs[0x37]==0x9F1F && (regs[0x33] & 0x40));
  assert(!(regs[0x33] & 0x20) && !regs[0x36] && regs[0x2B]==0);
}
int main(void) {
  BK48X9_Init(16,130);
  restoreRx(438750000u);
  assert(TX_DEVICE_Capabilities(145500000u).power_mask==7 && lastOffset==0xF0);
  assert(TX_DEVICE_Capabilities(438750000u).power_mask==7 && lastOffset==0x120);
  const uint32_t frequencies[]={136000000u,137000000u,155500000u,173999990u,
                                400000000u,435000000u,469999990u};
  const uint8_t highBias[]={145,145,150,146,145,150,146};
  for (unsigned i=0;i<sizeof(frequencies)/sizeof(*frequencies);++i) {
    TxPlan plan={.frequency_hz=frequencies[i],.power=TX_POWER_HIGH};
    resetTrace(); regs[0x74]=0xBEEF; regs[0x7E]=0xA5F7;
    assert(TX_DEVICE_Prepare(&plan) && paBias==highBias[i]);
    assert(writes[0].reg==0x33 && !(writes[0].value & 0x20));
    assert(!regs[0x36] && regs[0x50]==0xBB18 && regs[0x40]==0x3516);
    assert(regs[0x74]==0xBEEF && regs[0x7D]==0xE950 && regs[0x51]==0);
    assert(regs[0x38]==((plan.frequency_hz/10u)&0xFFFF) && regs[0x39]==((plan.frequency_hz/10u)>>16));
    assert(TX_DEVICE_Enable());
    assert((regs[0x33] & 3u)==1u); // K1 physical red, green off.
    assert(regs[0x7E]==0xA5EF && regs[0x52]==0x028F);
    assert((regs[0x33] & 0x0C)==(plan.frequency_hz<240000000u ? 0x04 : 0x08));
    assert((regs[0x36] & 0xFF)==(plan.frequency_hz<280000000u ? 0x88 : 0xA2));
    TX_DEVICE_Disable(); checkOff();
    assert(regs[0x30]==0 && regs[0x50]==0xBB18 && regs[0x51]==0);
    restoreRx(plan.frequency_hz);
  }
  TxPlan plan={.frequency_hz=438750000u,.power=TX_POWER_LOW,.tone=1};
  assert(TX_DEVICE_Prepare(&plan) && paBias==90 && regs[0x51]==0x9040 && regs[0x07]==1383);
  assert(TX_DEVICE_Enable());TX_DEVICE_Disable();checkOff();
  plan.power=TX_POWER_MEDIUM;plan.tone=105;
  assert(TX_DEVICE_Prepare(&plan) && paBias==100 && regs[0x51]==0xA033 && regs[0x07]==2775);
  uint32_t code=DCS_GetGolayCodeWord(TONE_Type(plan.tone),TONE_Code(plan.tone));
  assert(regs[0x08]==(0x8000u | ((code>>12)&0xFFFu)));
  TX_DEVICE_Disable();
  unsigned before=starts;
  stopOK=false; assert(!TX_DEVICE_Prepare(&plan));checkOff();stopOK=true;
  readOK=false;assert(!TX_DEVICE_Prepare(&plan));checkOff();readOK=true;
  uhf[0]=255;assert(TX_DEVICE_Capabilities(plan.frequency_hz).power_mask==6);
  plan.power=TX_POWER_LOW;assert(!TX_DEVICE_Prepare(&plan));checkOff();uhf[0]=90;
  plan.frequency_hz=470000000u;assert(!TX_DEVICE_Prepare(&plan));checkOff();
  plan.frequency_hz=438750001u;assert(!TX_DEVICE_Prepare(&plan));checkOff();
  plan.frequency_hz=438750000u;
  assert(TX_DEVICE_Prepare(&plan));pressed=false;
  assert(!TX_DEVICE_Enable());checkOff();pressed=true;
  assert(TX_DEVICE_Prepare(&plan));releaseDuringDelay=true;
  assert(!TX_DEVICE_Enable());checkOff();releaseDuringDelay=false;pressed=true;
  assert(TX_DEVICE_Prepare(&plan));voltageDropDuringDelay=true;
  assert(!TX_DEVICE_Enable());checkOff();voltageDropDuringDelay=false;gBatteryVoltage=760;
  assert(starts==before); // No failed attempt opened the physical PA gate.
  restoreRx(438750000u);
  return 0;
}
