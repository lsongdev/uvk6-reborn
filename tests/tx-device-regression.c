// Deliberately opt in for exercising the experimental TX HAL.
#define UVK6_ENABLE_TX 1
// Real board HAL and voice configuration; GPIO, EEPROM and RF writes mocked.
#include "../src/hal/tx-device.c"
#include "../src/drivers/bk4819/tx.c"
#include <assert.h>
#include <string.h>
DeviceConfig gDeviceConfig = {.preferences=CONFIG_DEFAULT_PREFERENCES};
uint16_t gBatteryVoltage=760;
static uint8_t calibration[9]={30,40,50,60,80,100,100,140,180};
static bool readOK=true, pressed=true, releaseDuringDelay, paOn, dspOn, rxEnabled=true;
static unsigned paStarts, invalidations, writeCount;
static uint16_t registers[128];
static uint32_t tuned, filterFrequency, calibrationAddress;
static uint8_t bias;
bool STORAGE_Read(StorageArea area, uint32_t offset, void *data, uint16_t size) {
  // Bands 2 and 5 of the PA table: 0x1EF0 and 0x1F20 on the UV-K5 layout.
  assert(area==STORAGE_CALIBRATION && size==9 && (offset==0xF0 || offset==0x120));
  calibrationAddress=0x1E00+offset;
  if (!readOK) return false;
  memcpy(data,calibration,size);return true;
}
bool GPIO_Read(GpioLine line) {
  assert(line==GPIO_PTT);return !pressed;
}
void BK4819_ToggleGpioOut(BK4819_GPIO_PIN_t pin, bool on) {
  if(pin==BK4819_GPIO1_PIN29_PA_ENABLE) {
    if(on) { assert(dspOn && !rxEnabled && bias && pressed);++paStarts; }
    paOn=on;
  } else { assert(pin==BK4819_GPIO0_PIN28_RX_ENABLE);rxEnabled=on; }
}
void BK4819_WriteRegister(BK4819_REGISTER_t reg, uint16_t value) {
  assert(!paOn);registers[reg]=value;++writeCount;
}
uint16_t BK4819_ReadRegister(BK4819_REGISTER_t reg) { return registers[reg]; }
void BK4819_EnterTxMute(void) { assert(!paOn); }
void BK4819_Idle(void) { assert(!paOn);dspOn=false; }
void BK4819_ExitSubAu(void) { assert(!paOn); }
void BK4819_SetRxMode(bool am) { assert(!paOn && !am); }
void BK4819_SetAF(BK4819_AF_Type_t af) { assert(!paOn && af==BK4819_AF_MUTE); }
void BK4819_DisableDTMF(void) { assert(!paOn); }
void BK4819_SetFilterBandwidth(BK4819_FilterBandwidth_t bw) {
  assert(!paOn && bw==BK4819_FILTER_BW_9k);
}
void BK4819_SelectFilter(uint32_t hz10) { assert(!paOn);filterFrequency=hz10; }
void BK4819_SetFrequency(uint32_t hz10) { assert(!paOn);tuned=hz10; }
void BK4819_SetCTCSSFrequency(uint32_t value) { assert(!paOn && value); }
void BK4819_SetCDCSSCodeWord(uint32_t value) { assert(!paOn && value); }
void BK4819_PrepareTransmit(void) { assert(!paOn);dspOn=true; }
void BK4819_SetupPowerAmplifier(uint8_t value, uint32_t hz10) {
  assert(!paOn && dspOn && hz10==tuned);bias=value;
}
void AUDIO_ToggleSpeaker(bool on) { assert(!on); }
void LED_SetGreen(bool on) { assert(!on); }
void LED_SetRed(bool on) { if(on) assert(paOn); }
void RX_DEVICE_InvalidateBk(void) { ++invalidations; }
void SYS_DelayMs(uint32_t ms) { assert(!paOn && ms==10);if(releaseDuringDelay) pressed=false; }
int main(void) {
  assert(TX_DEVICE_Capabilities(145500000).power_mask==7 && calibrationAddress==0x1EF0);
  assert(TX_DEVICE_Capabilities(438500000).power_mask==7 && calibrationAddress==0x1F20);
  assert(!TX_DEVICE_Capabilities(174000000).power_mask);
  assert(!TX_DEVICE_Capabilities(399999990).power_mask);
  assert(!TX_DEVICE_Capabilities(470000000).power_mask);
  assert(!TX_DEVICE_Capabilities(438500001).power_mask);
  TxPolicy policy=TX_DEVICE_Policy();assert(policy.count==2 && policy.authorized);
  readOK=false;assert(!TX_DEVICE_Capabilities(145500000).power_mask);readOK=true;
  calibration[4]=255;assert(TX_DEVICE_Capabilities(145500000).power_mask==5);calibration[4]=80;
  calibration[0]=0;assert(TX_DEVICE_Capabilities(145500000).power_mask==6);calibration[0]=30;
  TxPlan plan={.frequency_hz=438500000,.power=TX_POWER_MEDIUM,.tone=1};
  assert(TX_DEVICE_Prepare(&plan) && !paOn && !dspOn && prepared);
  assert(tuned==43850000 && filterFrequency==tuned && paBias==82);
  assert(registers[BK4819_REG_7D]==(0xE940|gDeviceConfig.preferences.mic) && gDeviceConfig.preferences.mic==16 && registers[0x74]==0xAF1F && writeCount);
  assert(TX_DEVICE_Enable() && paOn && paStarts==1 && dspOn);
  TX_DEVICE_Disable();assert(!paOn && !prepared && !dspOn && registers[BK4819_REG_36]==0);
  TX_DEVICE_Disable();assert(!paOn && invalidations>=2);
  plan.frequency_hz=150000000;plan.power=TX_POWER_HIGH;
  assert(TX_DEVICE_Prepare(&plan) && paBias==140 && calibrationAddress==0x1EF0);
  releaseDuringDelay=true;
  assert(!TX_DEVICE_Enable() && !paOn && !prepared && paStarts==1);
  releaseDuringDelay=false;pressed=true;
  gBatteryVoltage=599;assert(!TX_DEVICE_Prepare(&plan) && !paOn);
  gBatteryVoltage=901;assert(!TX_DEVICE_Prepare(&plan) && !paOn);
  gBatteryVoltage=0;assert(!TX_DEVICE_Prepare(&plan) && !paOn);
  gBatteryVoltage=760;pressed=false;assert(!TX_DEVICE_Prepare(&plan));pressed=true;
  readOK=false;assert(!TX_DEVICE_Prepare(&plan));readOK=true;
  plan.frequency_hz=95000000;assert(!TX_DEVICE_Prepare(&plan) && !paOn);
  plan.frequency_hz=136000000;plan.power=TX_POWER_LOW;
  assert(TX_DEVICE_Prepare(&plan) && paBias==30);TX_DEVICE_Disable();
  assert(interpolate(60,30,150000000,136000000,164000000)==45);
  return 0;
}
