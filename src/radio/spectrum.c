// Spectrum: sweeps BK over the window around the selected slot, muted.
// Reception is restored on stop unless the slot is tuned to the cursor.
#include "radio/radio-internal.h"

#if UVK6_ENABLE_SPECTRUM
Spectrum spectrum;
bool spectrumOn;
static uint32_t tunedAt;
static bool settling; // spectrum.next is tuned and settling.

void RADIO_StartSpectrum(void) {
  if (toneScanning || spectrumOn || capturing || seek.active || RADIO_TxBusy()) return;
  if (!pauseWatch()) return;
  RECEIVER_Mute();
  receivingSlot = UINT8_MAX;
  resetRds();
  if (!RX_DEVICE_SpectrumStart()) {
    if (!applyReceiver(NULL,&runtime.current)) RX_RuntimeStop(&runtime,stopReceiver,NULL);
    notify("RX error");
    return;
  }
  SPECTRUM_Start(&spectrum, activeSlot.current.frequency_hz,
                 activeSlot.current.parameters.step_hz,RECEIVER_Capabilities().bk_ranges);
  spectrumOn = true;
  settling = false;
  message = NULL;
  gRedrawScreen = true;
}

void RADIO_StopSpectrum(bool tune) {
  if (!spectrumOn) return;
  uint32_t cursor = SPECTRUM_Frequency(&spectrum, spectrum.cursor);
  RX_DEVICE_SpectrumStop();
  spectrumOn = false;
  // Back to the committed plan first, so a refused or failed tune still
  // leaves the slot playing.
  if (!applyReceiver(NULL, &runtime.current)) {
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    notify("RX error");
  } else {
    runtime.active = true;
    receivingSlot = selectedSlot;
    if (tune) RADIO_TuneTo(cursor);
  }
  delayWatch();
  gRedrawScreen = true;
}

const Spectrum *RADIO_Spectrum(void) { return spectrumOn ? &spectrum : NULL; }
void RADIO_SpectrumMove(bool up) { if (spectrumOn) SPECTRUM_Move(&spectrum, up); }
void RADIO_SpectrumZoom(bool wider) {
  if (spectrumOn && SPECTRUM_Zoom(&spectrum, wider)) settling = false;
}

// One point per call once it has settled; points BK cannot receive are
// recorded empty at once. The screen redraws every 16 points.
void spectrumUpdate(void) {
  if (!spectrumOn) return;
  uint32_t now = Now();
  if (settling) {
    if (now - tunedAt < 2u) return;
    uint16_t rssi = RX_DEVICE_SpectrumRssi();
    bool done = SPECTRUM_Record(&spectrum, rssi > 1u ? (uint8_t)(rssi >> 1) : 1u);
    if (done || !(spectrum.next & 15u)) gRedrawScreen = true;
    settling = false;
  }
  for (uint8_t skipped = 0; skipped < SPECTRUM_POINTS; ++skipped) {
    uint32_t hz = SPECTRUM_Frequency(&spectrum, spectrum.next);
    if (RADIO_MODEL_InRfRange(hz,spectrum.ranges)) {
      RX_DEVICE_SpectrumTune(hz);
      tunedAt = now;
      settling = true;
      return;
    }
    if (SPECTRUM_Record(&spectrum, 0) || !(spectrum.next & 15u)) gRedrawScreen = true;
  }
}
#else
void RADIO_StartSpectrum(void) { notify("Not supported"); }
void RADIO_StopSpectrum(bool tune) { (void)tune; }
const Spectrum *RADIO_Spectrum(void) { return NULL; }
void RADIO_SpectrumMove(bool up) { (void)up; }
void RADIO_SpectrumZoom(bool wider) { (void)wider; }
void spectrumUpdate(void) {}
#endif
