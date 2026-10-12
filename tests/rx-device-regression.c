// Mock concrete RF drivers, not the receive-planning and apply logic.
#include "../src/hal/rx-device.c"
#include <assert.h>

Settings gSettings;
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
void BK4819_Idle(void) { ++bkIdle; }
void BK4819_RX_TurnOn(void) {}
void BK4819_SetModulation(ModulationType mode) {
  assert(mode == MOD_FM || mode == MOD_AM); ++bkModes;
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
void SI47XX_PowerDown(void) { ++siPowerDowns; }
bool SI47XX_SetVolume(uint8_t v) {
  if (!v) ++siMuted; return siVolumeOk;
}
bool BK1080_Init(uint32_t hz10, bool enable) {
  assert(hz10 == 10000000u && enable);
  ++broadcastTunes; return broadcastOk;
}
bool BK1080_Mute(bool mute) {
  ++broadcastMutes;
  return mute || broadcastMuteOk;
}
static RxPlan plan(uint32_t hz, RxMode mode, RxDevice device) {
  return (RxPlan){device, hz, {mode, mode == RX_WFM ? 100000 : 12500,
    mode == RX_WFM ? 0 : mode == RX_NFM ? 12000 : 6000, 0, false}};
}
int main(void) {
  RxPlan bk = plan(100000000, RX_NFM, RX_DEVICE_BK);
  assert(RX_DEVICE_Apply(&bk, RX_DEVICE_BK));
  assert(bkTunes == 1 && lastBkHz10 == 10000000u && bkModes == 1);
  RxPlan bkNext = plan(100012500, RX_NFM, RX_DEVICE_BK);
  assert(RX_DEVICE_Apply(&bkNext, RX_DEVICE_BK));
  assert(bkTunes == 2 && bkModes == 1 && lastBkHz10 == 10001250u);
  gSettings.sqlOpenTime = 2;
  assert(RX_DEVICE_Apply(&bkNext, RX_DEVICE_BK));
  assert(bkTunes == 3 && bkModes == 2);
  gSettings.sqlOpenTime = 0;
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
  gSettings.si4732PowerOff = true;
  assert(RX_DEVICE_Apply(&bk, RX_DEVICE_SI));
  assert(siPowerDowns == 1 && bkTunes == 5);
  return 0;
}
