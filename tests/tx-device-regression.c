// Deliberately opt in for exercising the experimental TX HAL.
#define UVK6_ENABLE_TX 1
// Real board HAL and voice configuration; GPIO, EEPROM and RF writes mocked.
#include "../src/devices/tx-device.c"
#include "../src/drivers/bk48x9/tx.c"
#include "../src/boards/uvk6/calibration.c"
#include <assert.h>
#include <string.h>
const RadioSpan BK48X9_FREQUENCY_RANGES[RADIO_RF_RANGES] = {{18000000u,660000001u},{840000000u,1300000001u}};
DeviceConfig gDeviceConfig = {.preferences=CONFIG_DEFAULT_PREFERENCES};
uint16_t gBatteryVoltage=760;
static uint8_t calibration[9]={30,40,50,60,80,100,100,140,180};
static bool readOK=true, pressed=true, releaseDuringDelay, paOn, dspOn, rxEnabled=true;
static unsigned paStarts, invalidations, writeCount;
static uint16_t registers[128];
static uint32_t tuned, filterFrequency, calibrationAddress;
static uint8_t bias;
static bool stopOK=true;
bool RX_DEVICE_StopAll(void) { assert(!paOn); return stopOK; }
bool STORAGE_Read(StorageArea area, uint32_t offset, void *data, uint16_t size) {
  // All seven factory PA records in the UV-K5 layout.
  assert(area==STORAGE_CALIBRATION && size==9 && (offset>=0xD0 && offset<=0x130 && !(offset % 16u)));
  calibrationAddress=0x1E00+offset;
  if (!readOK) return false;
  memcpy(data,calibration,size);return true;
}
bool GPIO_Read(GpioLine line) {
  assert(line==GPIO_PTT);return !pressed;
}
void BK48X9_ToggleGpioOut(BK48X9_GPIO_PIN_t pin, bool on) {
  if(pin==BK48X9_GPIO1_PIN29_PA_ENABLE) {
    if(on) { assert(dspOn && !rxEnabled && bias && pressed);++paStarts; }
    paOn=on;
  } else { assert(pin==BK48X9_GPIO0_PIN28_RX_ENABLE);rxEnabled=on; }
}
void BK48X9_WriteRegister(BK48X9_REGISTER_t reg, uint16_t value) {
  assert(!paOn);registers[reg]=value;++writeCount;
}
uint16_t BK48X9_ReadRegister(BK48X9_REGISTER_t reg) { return registers[reg]; }
void BK48X9_EnterTxMute(void) { assert(!paOn); }
void BK48X9_Idle(void) { assert(!paOn);dspOn=false; }
void BK48X9_ExitSubAu(void) { assert(!paOn); }
void BK48X9_SetRxMode(bool am) { assert(!paOn && !am); }
void BK48X9_SetAF(BK48X9_AF_Type_t af) { assert(!paOn && af==BK48X9_AF_MUTE); }
void BK48X9_DisableDTMF(void) { assert(!paOn); }
void BK48X9_SetFilterBandwidth(BK48X9_FilterBandwidth_t bw) {
  assert(!paOn && bw==BK48X9_FILTER_BW_9k);
}
// BK4819 backend TX audio (drivers/bk48x9/bk4819.c), linked by RADIO_CHIP.
void BK48X9_ConfigureMicrophone(uint8_t mic) {
  BK48X9_WriteRegister(BK48X9_REG_19, 0x1041);
  BK48X9_WriteRegister(BK48X9_REG_7D, 0xE940 | (mic & 31u));
  BK48X9_WriteRegister(0x74, 0xAF1F);
}
void BK48X9_ConfigureTxAudio(uint8_t mic, uint16_t deviation) {
  BK48X9_SetFilterBandwidth(BK48X9_FILTER_BW_9k);
  BK48X9_ConfigureMicrophone(mic);
  BK48X9_WriteRegister(BK48X9_REG_40,(BK48X9_ReadRegister(BK48X9_REG_40)&~0x7FFu) | (deviation&0x7FFu) | (1u<<12));
}
void BK48X9_SelectFilter(uint32_t hz10, uint32_t bound) {
  assert(!paOn && bound==PREFERENCES_FILTER_BOUND_240);filterFrequency=hz10;
}
uint32_t PREFERENCES_FilterBound(void) { return PREFERENCES_FILTER_BOUND_240; }
void BK48X9_SetFrequency(uint32_t hz10) { assert(!paOn);tuned=hz10; }
void BK48X9_SetCTCSSFrequency(uint32_t value) { assert(!paOn && value); }
void BK48X9_SetCDCSSCodeWord(uint32_t value) { assert(!paOn && value); }
void BK48X9_StartTxTail(bool digital) { (void)digital; }
void BK48X9_PrepareTransmit(void) { assert(!paOn);dspOn=true; }
void BK48X9_SetupPowerAmplifier(uint8_t value, uint32_t hz10) {
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
  assert(TX_DEVICE_Capabilities(174000000).power_mask==7 && calibrationAddress==0x1F00);
  assert(TX_DEVICE_Capabilities(399999990).power_mask==7 && calibrationAddress==0x1F10);
  assert(TX_DEVICE_Capabilities(470000000).power_mask==7 && calibrationAddress==0x1F30);
  assert(!TX_DEVICE_Capabilities(438500001).power_mask);
  readOK=false;assert(!TX_DEVICE_Capabilities(145500000).power_mask);readOK=true;
  calibration[4]=255;assert(TX_DEVICE_Capabilities(145500000).power_mask==5);calibration[4]=80;
  calibration[0]=0;assert(TX_DEVICE_Capabilities(145500000).power_mask==6);calibration[0]=30;
  TxPlan plan={.frequency_hz=438500000,.power=TX_POWER_MEDIUM,.tone=1};
  assert(TX_DEVICE_Prepare(&plan) && !paOn && !dspOn && prepared);
  assert(tuned==43850000 && filterFrequency==tuned && paBias==82);
  assert(registers[BK48X9_REG_7D]==(0xE940|gDeviceConfig.preferences.mic) && gDeviceConfig.preferences.mic==16 && registers[0x74]==0xAF1F && writeCount);
  assert(TX_DEVICE_Enable() && paOn && paStarts==1 && dspOn);
  TX_DEVICE_Disable();assert(!paOn && !prepared && !dspOn && registers[BK48X9_REG_36]==0);
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
  stopOK=false;assert(!TX_DEVICE_Prepare(&plan) && !prepared && !paOn);stopOK=true;
  plan.frequency_hz=700000000;assert(!TX_DEVICE_Prepare(&plan) && !paOn);
  plan.frequency_hz=136000000;plan.power=TX_POWER_LOW;
  assert(TX_DEVICE_Prepare(&plan) && paBias==30);TX_DEVICE_Disable();
  assert(interpolate(60,30,150000000,136000000,164000000)==45);
  const uint32_t expanded[]={18000000u,95000000u,108000000u,174000000u,350000000u,
    399999990u,470000000u,600000000u,660000000u,840000000u,1300000000u};
  const uint16_t offsets[]={0xD0,0xD0,0xE0,0x100,0x110,0x110,0x130,0x130,0x130,0x130,0x130};
  for (unsigned i=0;i<sizeof(expanded)/sizeof(*expanded);++i) {
    plan.frequency_hz=expanded[i];
    assert(TX_DEVICE_Prepare(&plan) && calibrationAddress==0x1E00u+offsets[i]);
    if (i==0) assert(paBias==30); // Below the first anchor: clamp, no extrapolation.
    if (i>=7) assert(paBias==50); // Above the final anchor: clamp.
    assert(TX_DEVICE_Enable() && paOn);
    TX_DEVICE_Disable();assert(!paOn);
  }
  memset(calibration,0,sizeof(calibration));
  assert(!TX_DEVICE_Capabilities(350000000u).power_mask);
  plan.frequency_hz=350000000u;
  assert(!TX_DEVICE_Prepare(&plan) && !paOn);
  return 0;
}
