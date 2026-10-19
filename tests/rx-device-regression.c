#include "store/device-config.h"
// Mock concrete RF drivers, not the receive-planning and apply logic.
#include "../src/devices/rx-device.c"
#include <assert.h>

DeviceConfig gDeviceConfig;
static unsigned bkTunes, siTunes, broadcastTunes, bkIdle, siMuted, siPowerDowns;
static unsigned bkModes, lastBkHz10, siIo;
static bool siTuneOk = true, siBandwidthOk = true, siVolumeOk = true;
static bool broadcastOk = true, broadcastMuteOk = true;
static unsigned broadcastMutes;
static unsigned scanStarts, scanStops, modeInvalidations;
static uint32_t scanRaw;
static bool scanReady;
static bool rxPinEnabled;
static bool speakerOn, bkDac;
static unsigned speakerChanges;
static uint8_t siVolume;
static BK48X9_AF_Type_t bkAf;
static bool sharedBkPath, broadcastReceiving, broadcastMuted = true, standbyOK = true;
static void audioEvent(char c);
void AUDIO_ToggleSpeaker(bool on) {
  if (on) assert((unsigned)(bkDac && bkAf == BK48X9_AF_FM) + (siVolume != 0) +
                 (broadcastReceiving && !broadcastMuted) == 1); // No mixed outputs.
  if (speakerOn != on) ++speakerChanges;
  speakerOn=on; audioEvent(on ? 'S' : 's');
}
void BK48X9_SetAF(BK48X9_AF_Type_t af) { bkAf=af; audioEvent(af == BK48X9_AF_MUTE ? 'm' : 'F'); }
void BK48X9_ToggleAFDAC(bool on) { bkDac=on; audioEvent(on ? 'D' : 'd'); }
// Backend mute as the firmware links it: the BK4829 keeps its RX DAC powered.
void BK48X9_MuteAudio(void) {
  BK48X9_SetAF(BK48X9_AF_MUTE);
  if (UVK6_ENABLE_SI4732) BK48X9_ToggleAFDAC(false);
}

void BK48X9_StopScan(void) { ++scanStops; BK48X9_Idle(); }
void BK48X9_InvalidateRxMode(void) { ++modeInvalidations; }
void BK48X9_EnableFrequencyScanEx(FreqScanTime time) {
  assert(time==F_SC_T_0_2s); ++scanStarts;
}
bool BK48X9_GetFrequencyScanResult(uint32_t *raw) { *raw=scanRaw; return scanReady; }
uint32_t PREFERENCES_FilterBound(void) { return 24000000u; }
bool BK48X9_BandwidthFromHz(uint32_t hz, uint16_t *index) {
  if (hz != 12000 && hz != 6000) return false;
  *index = hz == 12000 ? 4 : 0; return true;
}
static uint32_t lastCtcss, lastDcs;
static unsigned toneResets, tonesOff;
void BK48X9_SetCTCSSFrequency(uint32_t hz10) { lastCtcss=hz10; }
void BK48X9_SetCDCSSCodeWord(uint32_t word) { lastDcs=word; }
void BK48X9_ExitSubAu(void) { ++tonesOff; }
void BK48X9_WriteRegister(BK48X9_REGISTER_t reg, uint16_t value) { (void)reg; (void)value; }
void BK48X9_CssResetDetection(bool digital) { (void)digital; ++toneResets; }
void BK48X9_CssOff(void) {}
static bool carrierOpen=true;
bool BK48X9_IsSquelchOpen(void) { return carrierOpen; }
void BK48X9_Idle(void) { ++bkIdle; bkDac=false; }
void BK48X9_RX_TurnOn(void) { bkDac=true; }
void BK48X9_SetRxMode(bool am) {
  (void)am; ++bkModes;
}
void BK48X9_SetFilterBandwidth(BK48X9_FilterBandwidth_t bw) { (void)bw; }
void BK48X9_SetAGC(uint8_t i) { (void)i; }
void BK48X9_SetAFC(uint8_t n) { (void)n; }
void BK48X9_SetAFCSpeed(uint8_t n) { (void)n; }
static SQL lastSquelch;
void BK48X9_SetupSquelch(SQL sq, uint8_t a, uint8_t b) { lastSquelch = sq; (void)a; (void)b; }
// UV-K1 calibration rows: 0x00 UHF and 0x60 VHF, six rows of 16 bytes.
static uint8_t calibration[0x200];
static bool calibrationReadable = true;
bool STORAGE_Read(StorageArea area, uint32_t offset, void *data, uint16_t size) {
  if (!calibrationReadable || area != STORAGE_CALIBRATION || offset + size > sizeof(calibration))
    return false;
  memcpy(data, calibration + offset, size); return true;
}
static Filter lastFilter = FILTER_OFF;
void BK48X9_SelectFilterEx(Filter filter) { lastFilter = filter; }
void BK48X9_TuneTo(uint32_t hz10, bool precise) {
  assert(!sharedBkPath || !broadcastReceiving); // A mute has not released RF.
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
bool RSQ_GET(void) { ++siIo; return rsqOK; }
bool SI47XX_ReadRDS(SI47XX_RdsStatus *status, bool clear) {
  (void)status; (void)clear; ++siIo; return true;
}
RSQStatus rsqStatus;
bool SI47XX_SeekStart(uint32_t lo, uint32_t hi, uint32_t step) {
  (void)lo; (void)hi; (void)step; ++siIo; return true;
}
SiSeekResult SI47XX_SeekPoll(uint32_t *frequency_hz) {
  ++siIo; *frequency_hz = 100100000; return SI_SEEK_FOUND;
}
void SI47XX_SeekCancel(void) { ++siIo; }
void SI47XX_PowerDown(void) { ++siPowerDowns; siVolume=0; }
bool SI47XX_SetVolume(uint8_t v) {
  ++siIo; if (!v) ++siMuted; siVolume=v;return siVolumeOk;
}
uint16_t BK1080_ReadRegister(BK1080_Register_t reg) {
  (void)reg; return 0x003f;
}
static unsigned broadcastParks;
bool BK1080_Standby(void) {
  ++broadcastParks;
  if (standbyOK) { broadcastReceiving=false; broadcastMuted=true; }
  return standbyOK;
}
bool BK1080_TuneTo(uint32_t hz10) {
  assert(hz10 == 10000000u);
  ++broadcastTunes;
  if (broadcastOk) { broadcastReceiving=true; broadcastMuted=false; }
  return broadcastOk;
}
bool BK1080_Mute(bool mute) {
  ++broadcastMutes;
  bool ok=mute || broadcastMuteOk;
  if (ok) broadcastMuted=mute;
  return ok;
}
static uint16_t watchRSSI=130;
static uint8_t watchNoise=20,watchGlitch=20;
static bool watchTone=true;
uint16_t BK48X9_GetRSSI(void) { return watchRSSI; }
uint8_t BK48X9_GetNoise(void) { return watchNoise; }
uint8_t BK48X9_GetGlitch(void) { return watchGlitch; }
bool BK48X9_CssIsDetected(bool digital) { (void)digital; return watchTone; }
SQL GetSql(uint8_t level) { return level ? (SQL){.ro=108,.no=44,.go=60} : (SQL){0}; }
static char audioOrder[16];
static unsigned audioCount;
static void audioEvent(char c) { if (audioCount < sizeof(audioOrder)) audioOrder[audioCount++] = c; }
void SYS_DelayMs(uint32_t ms) { assert(ms == 8); audioEvent('t'); }
uint16_t BK1080_GetRSSI(void) { return 30; }
uint8_t BK1080_GetSNR(void) { return 10; }
static RxPlan plan(uint32_t hz, RxMode mode, RxDevice device) {
  return (RxPlan){device, hz, {mode, mode == RX_WFM ? 100000 : 12500,
    mode == RX_WFM ? 0 : mode == RX_NFM ? 12000 : 6000, 0}};
}
static unsigned patchSources;
bool BOARD_BroadcastUsesBkVhfPath(void) { return sharedBkPath; }
bool isSi4732On;
// The board build fixes the chips: K6 SI4732 + BK4819, K1 BK1080 + BK4829.
BK48X9Capabilities BK48X9_GetCapabilities(void) {
  return UVK6_ENABLE_SI4732 ? (BK48X9Capabilities){4819, true, 0x3FF}
                            : (BK48X9Capabilities){4829, false, (1u << 0) | (1u << 4) | (1u << 9)};
}
bool RESOURCE_SsbPatchPresent(void) { return true; }
bool RESOURCE_SsbPatchRead(uint16_t offset, uint8_t *out, uint16_t size) {
  (void)offset; (void)out; (void)size; return true;
}
void SI47XX_SetPatchSource(SI47XX_PatchRead read, uint16_t length) {
  assert(read == RESOURCE_SsbPatchRead && length == RESOURCE_SSB_PATCH_BYTES); ++patchSources;
}
static uint8_t initMic; static uint16_t initDeviation;
void BK48X9_Init(uint8_t mic, uint16_t deviation) { initMic = mic; initDeviation = deviation; }
static int16_t rxTrim;
static uint8_t rxVolume, rxDac;
void BK48X9_SetRxCalibration(int16_t trim, uint8_t volume, uint8_t dac) {
  rxTrim = trim; rxVolume = volume; rxDac = dac;
}
void TX_DEVICE_Disable(void) {}

static void testCalibratedSquelch(void) {
#if UVK6_ENABLE_SI4732
  assert(squelchFor(4, 145500000u).ro == 108); // K6 keeps the formula.
#else
  // K1: level n reads byte n of each row; UHF from 0x00.
  for (unsigned i = 0; i < 6; ++i) {
    calibration[0x00 + 16 * i + 4] = (uint8_t)(0x40 + i);
    calibration[0x60 + 16 * i + 4] = (uint8_t)(0x50 + i);
  }
  SQL sq = squelchFor(4, 438500000u);
  // Reference tuning: RSSI open halved, noise and glitch open doubled.
  assert(sq.ro == 0x20 && sq.rc == 0x41 && sq.no == 0x7F && sq.nc == 0x43 && sq.gc == 0x44 && sq.go == 0x8A);
  sq = squelchFor(4, 145500000u);
  assert(sq.ro == 0x28 && sq.go == 0xAA); // VHF rows below 174 MHz.
  calibration[0x50 + 4] = 0xFF; // Erased byte: the formula takes over.
  assert(squelchFor(4, 438500000u).ro == 108);
  calibration[0x50 + 4] = 0x45;
  calibrationReadable = false;
  assert(squelchFor(4, 145500000u).ro == 108);
  calibrationReadable = true;
  assert(squelchFor(0, 145500000u).ro == 0); // Level 0 stays open.
#endif
}

static void testDetectionAndInit(void) {
  // Fixed by the build, then cached; the SSB patch source goes to the SI driver.
  ReceiverCapabilities c = RX_DEVICE_Capabilities();
  assert(c.si && !c.broadcast && c.ssb_patch && c.bk_am && patchSources == 1);
  c = RX_DEVICE_Capabilities();
  assert(c.si && patchSources == 1);
  gDeviceConfig.preferences.mic = 9; gDeviceConfig.preferences.deviation = 130;
  RX_DEVICE_Init();
  assert(initMic == 9 && initDeviation == 130 && !broadcastParks); // No BK1080 built in.
}

static void testUvk1(void) {
  ReceiverCapabilities c = RX_DEVICE_Capabilities();
  assert(!c.si && !c.ssb_patch && c.broadcast && !c.bk_am);
  assert(c.bk_bandwidth_mask == (RX_BK_BANDWIDTH_6K | RX_BK_BANDWIDTH_12K | RX_BK_BANDWIDTH_26K));
  RxPlan am = plan(125000000, RX_AM, RX_DEVICE_BK);
  assert(!RX_DEVICE_Activate(&am));
  assert(patchSources == 0); // No SI4732 driver path in this build.
  RxPlan unsupported = plan(100000000, RX_WFM, RX_DEVICE_SI);
  assert(!RX_DEVICE_Activate(&unsupported));
  assert(!siTunes && !bkTunes && !bkIdle && !siPowerDowns);
  RdsPacket packet;
  assert(!RX_DEVICE_ReadRDS(&packet, false));
  uint16_t rssi; bool found;
  uint32_t frequency;
  assert(!RX_DEVICE_Sample(&unsupported, &rssi, &found));
  assert(!RX_DEVICE_WatchSignal(&unsupported, &found));
  assert(!RX_DEVICE_SeekSignal(RX_DEVICE_SI, &found));
  assert(!RX_DEVICE_SeekNative(&unsupported));
  assert(!RX_DEVICE_SeekStart(&unsupported, 64000000, 108000000));
  assert(RX_DEVICE_SeekPoll(RX_DEVICE_SI, &frequency) == RX_SEEK_ERROR);
  RX_DEVICE_SeekCancel(RX_DEVICE_SI);
  assert(!RX_DEVICE_RouteAudio(RX_DEVICE_SI, true));
  isSi4732On = true; // Even stale chip state must not advertise an absent part.
  assert(!RX_DEVICE_SiActive());
  assert(siIo == 0 && !speakerOn);

  // Factory trim/gains, erased and unreadable fallback all use read-only IO.
  memset(calibration, 0xFF, sizeof(calibration));
  RX_DEVICE_Init();
  assert(rxTrim == 0 && rxVolume == 58 && rxDac == 8);
  memcpy(calibration + CALIBRATION_RX_MISC_OFFSET, "\xfe\xff\x46\0\x50\0\x2c\x0e", 8);
  RX_DEVICE_Init();
  assert(rxTrim == -2 && rxVolume == 44 && rxDac == 14);
  calibrationReadable = false;
  RX_DEVICE_Init();
  assert(rxTrim == 0 && rxVolume == 58 && rxDac == 8);
  calibrationReadable = true;

  // A frequency-only hop across 174 MHz must reload the other SQL table,
  // even though both frequencies use the same VHF front-end filter.
  RxPlan vhf = plan(173987500u, RX_NFM, RX_DEVICE_BK);
  vhf.parameters.squelch = 4;
  assert(RX_DEVICE_Activate(&vhf));
  unsigned before = bkModes;
  vhf.frequency_hz = 174000000u;
  assert(RX_DEVICE_Activate(&vhf) && bkModes == before + 1);
  vhf.frequency_hz += 12500u;
  assert(RX_DEVICE_Activate(&vhf) && bkModes == before + 1);

  // SQ0 must really bypass hardware SQL, with tone matching still enforced.
  vhf.parameters.squelch = 0; carrierOpen = false;
  assert(RX_DEVICE_Sample(&vhf, &rssi, &found) && found);
  vhf.parameters.rx_tone = 1; watchTone = false;
  assert(RX_DEVICE_Sample(&vhf, &rssi, &found) && !found);
  watchTone = true;
  assert(RX_DEVICE_Sample(&vhf, &rssi, &found) && found);

  // K1 keeps the RX DAC powered between SQL changes and selects full AF.
  audioCount = 0; bkDac = true;
  RX_DEVICE_RouteAudio(RX_DEVICE_BK, true);
  RX_DEVICE_RouteAudio(RX_DEVICE_BK, false);
  assert(bkDac && !speakerOn && audioCount > 0 && audioOrder[0] == 's');
}

static void testBroadcastPath(void) {
  RxPlan bk = plan(100000000, RX_NFM, RX_DEVICE_BK);
  uint16_t rssi; bool open;
  RxPlan fmRadio = plan(100000000, RX_WFM, RX_DEVICE_BROADCAST);
  broadcastOk = false;
  assert(!RX_DEVICE_Activate(&fmRadio));
  assert(broadcastTunes == 1);
  broadcastOk = true;
  broadcastMuteOk = false;
  assert(RX_DEVICE_Activate(&fmRadio)); // Prepared muted.
  assert(!RX_DEVICE_RouteAudio(RX_DEVICE_BROADCAST,true));
  assert(broadcastTunes == 2 && !speakerOn);
  broadcastMuteOk = true;
  assert(RX_DEVICE_Activate(&fmRadio));
  assert(broadcastTunes == 3);
  assert(RX_DEVICE_Activate(&bk));
  assert(bkTunes == 4 && lastBkHz10 == 10000000u);
  // UV-K1: broadcast needs the BK VHF LNA; no background BK retune meanwhile.
  sharedBkPath = true;
  assert(RX_DEVICE_Activate(&fmRadio));
  assert(broadcastTunes == 4 && lastFilter == FILTER_VHF);
  RxPlan uhfWatch = plan(438500000u, RX_NFM, RX_DEVICE_BK);
  unsigned bkTunesBefore = bkTunes;
  assert(!RX_DEVICE_Prepare(&uhfWatch) && bkTunes == bkTunesBefore);
  assert(lastFilter == FILTER_VHF);
  assert(RX_DEVICE_Activate(&bk)); // Leaving broadcast frees BK.
  assert(RX_DEVICE_Prepare(&uhfWatch));
  sharedBkPath = false;
  RxPlan fm = plan(100000000u, RX_WFM, RX_DEVICE_BROADCAST); fm.parameters.squelch=9;
  assert(RX_DEVICE_Activate(&fm) && RX_DEVICE_Sample(&fm,&rssi,&open) && open && rssi==30);

  // The device layer owns real path state; callers do not supply a guessed
  // previous chip. A standby NACK must stop a conflicting foreground tune.
  RxPlan watch = plan(145500000, RX_NFM, RX_DEVICE_BK);
  unsigned tunesBefore;
  sharedBkPath=true;
  assert(!RX_DEVICE_CanRunTogether(RX_DEVICE_BK,RX_DEVICE_BROADCAST));
  assert(!RX_DEVICE_CanRunTogether(RX_DEVICE_BROADCAST,RX_DEVICE_BK));
  assert(RX_DEVICE_Activate(&fmRadio));
  assert(RX_DEVICE_RouteAudio(RX_DEVICE_BROADCAST,true) && broadcastReceiving);
  standbyOK=false; tunesBefore=bkTunes;
  assert(!RX_DEVICE_Activate(&watch));
  assert(bkTunes==tunesBefore && broadcastReceiving && !speakerOn);
  assert(!RX_DEVICE_Prepare(&watch)); // Mute did not release the front end.
  standbyOK=true;
  assert(RX_DEVICE_Activate(&fmRadio)); // Explicit recovery to the old FM.
  for (unsigned i=0; i<20; ++i) {
    assert(RX_DEVICE_Activate(&fmRadio));
    assert(RX_DEVICE_RouteAudio(RX_DEVICE_BROADCAST,true) && speakerOn);
    unsigned parks=broadcastParks;
    watch.frequency_hz=(i&1u) ? 145500000u : 438500000u;
    assert(RX_DEVICE_Activate(&watch));
    assert(!broadcastReceiving && broadcastParks==parks+1 && lastBkHz10==watch.frequency_hz/10u);
    assert(RX_DEVICE_RouteAudio(RX_DEVICE_BK,true) && speakerOn);
  }
  // FC cannot use the same shared RF path underneath FM; cancel/restore
  // activates FM again instead of merely restoring its public slot mirror.
  assert(RX_DEVICE_Activate(&fmRadio));
  assert(RX_DEVICE_CaptureStart(CAPTURE_RANGE_VHF) && !broadcastReceiving);
  RX_DEVICE_CaptureStop();
  assert(RX_DEVICE_Activate(&fmRadio) && broadcastReceiving);
  assert(RX_DEVICE_RouteAudio(RX_DEVICE_BROADCAST,true));
  standbyOK=false;
  assert(!RX_DEVICE_StopAll() && !speakerOn && !audioOpen && broadcastReceiving);
  assert(runningDevices & (1u << RX_DEVICE_BROADCAST)); // Failed release stays tracked.
  standbyOK=true;
  assert(RX_DEVICE_StopAll() && !runningDevices && !broadcastReceiving && !speakerOn);
  // TX release restores the same channel through full RX activation.
  assert(RX_DEVICE_Activate(&watch) && rxPinEnabled && bkDac);
}

int main(void) {
#if !UVK6_ENABLE_SI4732
  testUvk1();
  testCalibratedSquelch();
  testBroadcastPath();
  return 0;
#else
  testCalibratedSquelch();
  testDetectionAndInit();
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
  assert(RX_DEVICE_Activate(&bk));
  assert(bkTunes == 1 && lastBkHz10 == 10000000u && bkModes == 1);
  RxPlan bkNext = plan(100012500, RX_NFM, RX_DEVICE_BK);
  assert(RX_DEVICE_Activate(&bkNext));
  assert(bkTunes == 2 && bkModes == 1 && lastBkHz10 == 10001250u);
  gDeviceConfig.preferences.sql_open_time = 2;
  assert(RX_DEVICE_Activate(&bkNext));
  assert(bkTunes == 3 && bkModes == 2);
  gDeviceConfig.preferences.sql_open_time = 0;
  RxPlan wfm = plan(100000000, RX_WFM, RX_DEVICE_SI);
  assert(RX_DEVICE_Activate(&wfm));
  assert(bkIdle == 1 && siTunes == 1);
  RxPlan unsupported = plan(100000000, RX_WFM, RX_DEVICE_BK);
  unsigned beforeInvalid = siMuted;
  assert(!RX_DEVICE_Activate(&unsupported));
  assert(siMuted == beforeInvalid && bkTunes == 3); // Validate before power switch.
  siTuneOk = false;
  assert(!RX_DEVICE_Activate(&wfm));
  assert(siTunes == 2 && bkTunes == 3);
  siTuneOk = true;
  siVolumeOk = false;
  assert(!RX_DEVICE_Activate(&wfm));
  siVolumeOk = true;
  assert(RX_DEVICE_Activate(&wfm));
  // SI4732 build: the BK1080 path is not compiled in.
  RxPlan fmRadio = plan(100000000, RX_WFM, RX_DEVICE_BROADCAST);
  assert(!RX_DEVICE_Activate(&fmRadio) && !broadcastTunes);
  assert(RX_DEVICE_Activate(&bk));
  assert(bkTunes == 4 && lastBkHz10 == 10000000u);
  gDeviceConfig.preferences.si4732_power_off = true;
  assert(RX_DEVICE_Prepare(&native));
  assert(RX_DEVICE_Activate(&bk));
  // SI powers down on the handoff; BK already holds this plan, so no retune.
  assert(siPowerDowns == 1 && bkTunes == 4);
  bk.parameters.rx_tone=1;
  unsigned resetBefore=toneResets;
  assert(RX_DEVICE_Activate(&bk));
  assert(lastCtcss==670 && toneResets==resetBefore+1);
  assert(RX_DEVICE_Activate(&bk));assert(toneResets==resetBefore+1);
  bk.frequency_hz+=12500;assert(RX_DEVICE_Activate(&bk));
  assert(toneResets==resetBefore+2);
  bk.parameters.rx_tone=51;assert(RX_DEVICE_Activate(&bk));
  uint32_t normal=lastDcs;
  bk.parameters.rx_tone=155;assert(RX_DEVICE_Activate(&bk));
  assert(lastDcs==(normal^0x7fffffu));
  bk.parameters.rx_tone=0;unsigned offBefore=tonesOff;
  assert(RX_DEVICE_Activate(&bk));assert(tonesOff==offBefore+1);
  bk.parameters.rx_tone=1;bk.parameters.mode=RX_AM;
  assert(!RX_DEVICE_Activate(&bk));
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
  // Capture owns BK physically even when the public mirror still names SI.
  gDeviceConfig.preferences.si4732_power_off=false;
  assert(RX_DEVICE_Activate(&native));
  assert(RX_DEVICE_RouteAudio(RX_DEVICE_SI,true));
  unsigned muteBefore=siMuted, invalidateBefore=modeInvalidations;
  RX_DEVICE_CaptureStart(CAPTURE_RANGE_VHF);
  assert(siMuted==muteBefore+1 && scanStarts==1 && modeInvalidations==invalidateBefore+1);
  uint32_t captured=0;
  assert(RX_DEVICE_CapturePoll(CAPTURE_RANGE_VHF,&captured)==CAPTURE_BUSY);
  scanReady=true;scanRaw=14550000;
  assert(RX_DEVICE_CapturePoll(CAPTURE_RANGE_VHF,&captured)==CAPTURE_SAMPLE && captured==145500000);
  assert(RX_DEVICE_CapturePoll(CAPTURE_RANGE_UHF,&captured)==CAPTURE_INVALID);
  scanRaw=43850000;
  assert(RX_DEVICE_CapturePoll(CAPTURE_RANGE_UHF,&captured)==CAPTURE_SAMPLE && captured==438500000);
  scanRaw=UINT32_MAX;
  assert(RX_DEVICE_CapturePoll(CAPTURE_RANGE_UHF,&captured)==CAPTURE_INVALID);
  assert(RX_DEVICE_CapturePoll(CAPTURE_RANGE_UHF,NULL)==CAPTURE_INVALID);
  RX_DEVICE_CaptureStart(CAPTURE_RANGE_UHF);
  assert(!broadcastMutes);
  // Same-frequency restoration must reconfigure mode and tune, bypassing cache.
  watch.parameters.rx_tone=0;
  assert(RX_DEVICE_Activate(&watch));
  unsigned modesBefore=bkModes, tunesBefore=bkTunes;
  RX_DEVICE_CaptureStop();RX_DEVICE_CaptureStop();
  assert(RX_DEVICE_Activate(&watch));
  assert(bkModes==modesBefore+1 && bkTunes==tunesBefore+1 && scanStops>=4);
  // Concurrent prepare must not idle the other receiver; routing must not tune.
  siVolumeOk=true;rsqOK=true;
  unsigned beforeBkIdle=bkIdle;
  native.parameters.squelch=0;
  assert(RX_DEVICE_Prepare(&native) && siVolume==0 && bkIdle==beforeBkIdle);
  unsigned beforeSiMuted=siMuted;
  assert(RX_DEVICE_Prepare(&watch) && !bkDac && siMuted==beforeSiMuted);
  unsigned beforeBkTunes=bkTunes,beforeSiTunes=siTunes;
  assert(RX_DEVICE_RouteAudio(RX_DEVICE_SI,true));
  assert(speakerOn && siVolume==63 && !bkDac);
  unsigned changes=speakerChanges, backgroundTunes=siTunes;
  assert(RX_DEVICE_Prepare(&watch));
  assert(speakerOn && siVolume==63 && speakerChanges==changes && siTunes==backgroundTunes);
  assert(!RX_DEVICE_Activate(&unsupported));
  assert(speakerOn && siVolume==63 && speakerChanges==changes); // Validate before muting.
  beforeBkTunes=bkTunes;
  assert(RX_DEVICE_RouteAudio(RX_DEVICE_BK,true));
  assert(speakerOn && siVolume==0 && bkDac);
  assert(bkTunes==beforeBkTunes && siTunes==beforeSiTunes);
  assert(RX_DEVICE_RouteAudio(RX_DEVICE_BK,false));
  assert(!speakerOn && !bkDac && !siVolume);
  siVolumeOk=false;
  assert(!RX_DEVICE_RouteAudio(RX_DEVICE_BK,true) && !speakerOn && !bkDac);
  siVolumeOk=true;
  rxPinEnabled=false;
  modesBefore=bkModes;tunesBefore=bkTunes;
  RX_DEVICE_InvalidateBk();
  assert(RX_DEVICE_Activate(&watch));
  assert(rxPinEnabled && bkModes==modesBefore+1 && bkTunes==tunesBefore+1);
  // Squelch sample: BK requires carrier and the configured tone.
  RxPlan toned = plan(438500000u, RX_NFM, RX_DEVICE_BK);
  toned.parameters.squelch = 4;
  toned.parameters.rx_tone = 1;
  uint16_t rssi; bool open;
  carrierOpen=true; watchTone=false;
  assert(RX_DEVICE_Sample(&toned,&rssi,&open) && !open && rssi==watchRSSI);
  watchTone=true; assert(RX_DEVICE_Sample(&toned,&rssi,&open) && open);
  carrierOpen=false; assert(RX_DEVICE_Sample(&toned,&rssi,&open) && !open);
  toned.parameters.rx_tone=0; carrierOpen=true;
  assert(RX_DEVICE_Sample(&toned,&rssi,&open) && open);
  // SI/BK1080 open on SNR above the squelch level; unanswered SI stays closed.
  RxPlan si = plan(100000000u, RX_WFM, RX_DEVICE_SI);
  rsqStatus.resp.RSSI=40; rsqStatus.resp.SNR=6; si.parameters.squelch=5;
  assert(RX_DEVICE_Sample(&si,&rssi,&open) && open && rssi==40);
  si.parameters.squelch=6; assert(RX_DEVICE_Sample(&si,&rssi,&open) && !open);
  rsqOK=false; assert(!RX_DEVICE_Sample(&si,&rssi,&open) && !open && !rssi); rsqOK=true;
  // Speaker gate: DAC before speaker on open, speaker first on close.
  bkDac=false; audioCount=0;
  RX_DEVICE_RouteAudio(RX_DEVICE_BK,true); RX_DEVICE_RouteAudio(RX_DEVICE_BK,false);
  assert(!speakerOn && !bkDac && audioCount > 0 && audioOrder[0] == 's');
  assert(RX_DEVICE_Activate(&si));
  audioCount=0;
  RX_DEVICE_RouteAudio(RX_DEVICE_SI,true); RX_DEVICE_RouteAudio(RX_DEVICE_SI,false);
  assert(!speakerOn && siVolume==0 && audioCount > 0 && audioOrder[0] == 's');

  assert(RX_DEVICE_StopChip(RX_DEVICE_SI));
  return 0;
#endif
}

void BK48X9_ToggleGpioOut(BK48X9_GPIO_PIN_t pin, bool enabled) { assert(pin==BK48X9_GPIO0_PIN28_RX_ENABLE); rxPinEnabled=enabled; }
