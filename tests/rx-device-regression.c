#include "core/device-config.h"
// Mock concrete RF drivers, not the receive-planning and apply logic.
#include "../src/hal/rx-device.c"
#include <assert.h>

Settings gSettings;
DeviceConfig gDeviceConfig;
static unsigned bkTunes, siTunes, broadcastTunes, bkIdle, siMuted, siPowerDowns;
static unsigned bkModes, lastBkHz10;
static bool siTuneOk = true, siBandwidthOk = true, siVolumeOk = true;
static bool broadcastOk = true, broadcastMuteOk = true;
static unsigned broadcastMutes;
uint32_t SETTINGS_GetFilterBound(void) { return 24000000u; }
bool BK4819_BandwidthFromHz(uint32_t hz, uint16_t *index) {
  if (hz != 12000 && hz != 6000) return false;
  *index = hz == 12000 ? 4 : 0; return true;
}
static uint32_t lastCtcss, lastDcs;
static unsigned toneResets, tonesOff;
void BK4819_SetCTCSSFrequency(uint32_t hz10) { lastCtcss=hz10; }
void BK4819_SetCDCSSCodeWord(uint32_t word) { lastDcs=word; }
void BK4819_ExitSubAu(void) { ++tonesOff; }
void BK4819_WriteRegister(BK4819_REGISTER_t reg, uint16_t value) { (void)reg; (void)value; }
void BK4819_CssResetDetection(bool digital) { (void)digital; ++toneResets; }
bool BK4819_IsSquelchOpen(void) { return true; }
void BK4819_Idle(void) { ++bkIdle; }
void BK4819_RX_TurnOn(void) {}
void BK4819_SetRxMode(bool am) {
  (void)am; ++bkModes;
}
void BK4819_SetFilterBandwidth(BK4819_FilterBandwidth_t bw) { (void)bw; }
void BK4819_SetAGC(bool b, uint8_t i) { (void)b; (void)i; }
void BK4819_SetAFC(uint8_t n) { (void)n; }
void BK4819_SetAFCSpeed(uint8_t n) { (void)n; }
void BK4819_Squelch(uint8_t v, uint8_t a, uint8_t b) { (void)v; (void)a; (void)b; }
void BK4819_SelectFilterEx(Filter filter) { (void)filter; }
void BK4819_TuneTo(uint32_t hz10, bool precise) {
  ++bkTunes; lastBkHz10 = hz10; assert(precise);
}
bool SI47XX_BandwidthFromHz(uint32_t hz, bool ssb, uint16_t *bw) {
  (void)ssb; if (hz != 6000 && hz != 3000) return false;
  *bw = 3; return true;
}
bool SI47XX_SwitchMode(SI47XX_MODE m) {
  assert(m == SI47XX_FM || m == SI47XX_AM ||
         m == SI47XX_USB || m == SI47XX_LSB); return true;
}
bool SI47XX_TuneTo(uint32_t hz10) { (void)hz10; ++siTunes; return siTuneOk; }
bool SI47XX_SetSsbBandwidth(SI47XX_SsbFilterBW bw) {
  (void)bw; return siBandwidthOk;
}
bool SI47XX_SetBandwidth(SI47XX_FilterBW bw, bool amp) {
  (void)bw; (void)amp; return siBandwidthOk;
}
static bool rsqOK=true;
bool RSQ_GET(void) { return rsqOK; }
RSQStatus rsqStatus;
bool SI47XX_SeekStart(uint32_t lo, uint32_t hi, uint32_t step) {
  (void)lo; (void)hi; (void)step; return true;
}
SiSeekResult SI47XX_SeekPoll(uint32_t *frequency_hz) {
  *frequency_hz = 100100000; return SI_SEEK_FOUND;
}
void SI47XX_SeekCancel(void) {}
void SI47XX_PowerDown(void) { ++siPowerDowns; }
bool SI47XX_SetVolume(uint8_t v) {
  if (!v) ++siMuted; return siVolumeOk;
}
uint16_t BK1080_ReadRegister(BK1080_Register_t reg) {
  (void)reg; return 0x003f;
}
bool BK1080_Init(uint32_t hz10, bool enable) {
  assert(hz10 == 10000000u && enable);
  ++broadcastTunes; return broadcastOk;
}
bool BK1080_Mute(bool mute) {
  ++broadcastMutes;
  return mute || broadcastMuteOk;
}
static uint16_t watchRSSI=130;
static uint8_t watchNoise=20,watchGlitch=20;
static bool watchTone=true;
uint16_t BK4819_GetRSSI(void) { return watchRSSI; }
uint8_t BK4819_GetNoise(void) { return watchNoise; }
uint8_t BK4819_GetGlitch(void) { return watchGlitch; }
bool BK4819_CssIsDetected(bool digital) { (void)digital; return watchTone; }
SQL GetSql(uint8_t level) {
  assert(level>=4); return (SQL){.ro=108,.no=44,.go=60};
}
static RxPlan plan(uint32_t hz, RxMode mode, RxDevice device) {
  return (RxPlan){device, hz, {mode, mode == RX_WFM ? 100000 : 12500,
    mode == RX_WFM ? 0 : mode == RX_NFM ? 12000 : 6000, 0}};
}
int main(void) {
  RxPlan bk = plan(100000000, RX_NFM, RX_DEVICE_BK);
  RxPlan native = plan(100000000, RX_WFM, RX_DEVICE_SI);
  uint32_t found = 0;
  assert(RX_DEVICE_SeekNative(&native));
  assert(RX_DEVICE_SeekStart(&native, 64000000, 108000000));
  assert(RX_DEVICE_SeekPoll(RX_DEVICE_SI, &found) == RX_SEEK_FOUND);
  assert(found == 100100000);
  assert(!RX_DEVICE_SeekNative(&bk));
  assert(!RX_DEVICE_SeekStart(&bk, 64000000, 108000000));
  bool signal;
  assert(RX_DEVICE_SeekSignal(RX_DEVICE_BK, &signal) && signal);
  assert(RX_DEVICE_Apply(&bk, RX_DEVICE_BK));
  assert(bkTunes == 1 && lastBkHz10 == 10000000u && bkModes == 1);
  RxPlan bkNext = plan(100012500, RX_NFM, RX_DEVICE_BK);
  assert(RX_DEVICE_Apply(&bkNext, RX_DEVICE_BK));
  assert(bkTunes == 2 && bkModes == 1 && lastBkHz10 == 10001250u);
  gDeviceConfig.preferences.sql_open_time = 2;
  assert(RX_DEVICE_Apply(&bkNext, RX_DEVICE_BK));
  assert(bkTunes == 3 && bkModes == 2);
  gDeviceConfig.preferences.sql_open_time = 0;
  RxPlan wfm = plan(100000000, RX_WFM, RX_DEVICE_SI);
  assert(RX_DEVICE_Apply(&wfm, RX_DEVICE_BK));
  assert(bkIdle == 1 && siTunes == 1);
  RxPlan unsupported = plan(100000000, RX_WFM, RX_DEVICE_BK);
  assert(!RX_DEVICE_Apply(&unsupported, RX_DEVICE_SI));
  assert(siMuted == 0 && bkTunes == 3); // Validate before power switch.
  siTuneOk = false;
  assert(!RX_DEVICE_Apply(&wfm, RX_DEVICE_SI));
  assert(siTunes == 2 && bkTunes == 3);
  siTuneOk = true;
  siVolumeOk = false;
  assert(!RX_DEVICE_Apply(&wfm, RX_DEVICE_SI));
  siVolumeOk = true;
  assert(RX_DEVICE_Apply(&wfm, RX_DEVICE_SI));
  RxPlan fmRadio = plan(100000000, RX_WFM, RX_DEVICE_BROADCAST);
  unsigned mutedBeforeSwitch = siMuted;
  broadcastOk = false;
  assert(!RX_DEVICE_Apply(&fmRadio, RX_DEVICE_SI));
  assert(siMuted == mutedBeforeSwitch + 1 && broadcastTunes == 1);
  broadcastOk = true;
  broadcastMuteOk = false;
  assert(!RX_DEVICE_Apply(&fmRadio, RX_DEVICE_SI));
  assert(broadcastTunes == 2 && broadcastMutes >= 3);
  broadcastMuteOk = true;
  assert(RX_DEVICE_Apply(&fmRadio, RX_DEVICE_SI));
  assert(broadcastTunes == 3);
  assert(RX_DEVICE_Apply(&bk, RX_DEVICE_BROADCAST));
  assert(bkTunes == 4 && lastBkHz10 == 10000000u);
  gDeviceConfig.preferences.si4732_power_off = true;
  assert(RX_DEVICE_Apply(&bk, RX_DEVICE_SI));
  assert(siPowerDowns == 1 && bkTunes == 5);
  bk.parameters.rx_tone=1;
  unsigned resetBefore=toneResets;
  assert(RX_DEVICE_Apply(&bk,RX_DEVICE_BK));
  assert(lastCtcss==670 && toneResets==resetBefore+1);
  assert(RX_DEVICE_Apply(&bk,RX_DEVICE_BK));assert(toneResets==resetBefore+1);
  bk.frequency_hz+=12500;assert(RX_DEVICE_Apply(&bk,RX_DEVICE_BK));
  assert(toneResets==resetBefore+2);
  bk.parameters.rx_tone=51;assert(RX_DEVICE_Apply(&bk,RX_DEVICE_BK));
  uint32_t normal=lastDcs;
  bk.parameters.rx_tone=155;assert(RX_DEVICE_Apply(&bk,RX_DEVICE_BK));
  assert(lastDcs==(normal^0x7fffffu));
  bk.parameters.rx_tone=0;unsigned offBefore=tonesOff;
  assert(RX_DEVICE_Apply(&bk,RX_DEVICE_BK));assert(tonesOff==offBefore+1);
  bk.parameters.rx_tone=1;bk.parameters.mode=RX_AM;
  assert(!RX_DEVICE_Apply(&bk,RX_DEVICE_BK));
  // SQ0/always-open audio cannot classify arbitrary noise as a watch hit.
  RxPlan watch=plan(145500000,RX_NFM,RX_DEVICE_BK);
  assert(RX_DEVICE_WatchSignal(&watch,&signal) && signal);
  watchRSSI=50;assert(RX_DEVICE_WatchSignal(&watch,&signal) && !signal);
  watchRSSI=130;watchNoise=100;assert(RX_DEVICE_WatchSignal(&watch,&signal) && !signal);
  watchNoise=20;watchGlitch=100;assert(RX_DEVICE_WatchSignal(&watch,&signal) && !signal);
  watchGlitch=20;watch.parameters.rx_tone=1;watchTone=false;
  assert(RX_DEVICE_WatchSignal(&watch,&signal) && !signal);
  watchTone=true;assert(RX_DEVICE_WatchSignal(&watch,&signal) && signal);
  rsqStatus.resp.RSSI=20;rsqStatus.resp.SNR=8;
  assert(RX_DEVICE_WatchSignal(&native,&signal) && signal);
  native.parameters.squelch=8;assert(RX_DEVICE_WatchSignal(&native,&signal) && !signal);
  rsqOK=false;assert(!RX_DEVICE_WatchSignal(&native,&signal));
  assert(!RX_DEVICE_WatchSignal(NULL,&signal) && !RX_DEVICE_WatchSignal(&watch,NULL));
  return 0;
}
