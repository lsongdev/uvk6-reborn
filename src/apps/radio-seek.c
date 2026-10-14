// Seek: tunes candidates temporarily; only an accepted station is committed.
#include "apps/radio-internal.h"

// Search never mutates activeSlot/EEPROM until one final station is accepted.
typedef enum { SEEK_STEP, SEEK_SETTLE, SEEK_VERIFY,
               SEEK_NATIVE_START, SEEK_NATIVE_POLL, SEEK_WRAP_SETTLE } SeekPhase;
Seek seek;
static SeekPhase seekPhase;
static RxPlan seekOrigin;
static bool seekNative;
static uint32_t seekAt, seekDeadline;

// Forward-only seek owns temporary RF tuning; RxRuntime retains the last
// committed plan so cancel/exhaustion never writes the EEPROM.
void finishSeek(bool found, uint32_t frequency_hz, const char *status) {
  // SEEK_Next/SEEK_Wrap may already have marked exhaustion.
  if (seekNative)
    RX_DEVICE_SeekCancel(seekOrigin.device);
  SEEK_Stop(&seek);
  resetRds();
  gRedrawScreen = true;

  if (found) {
    RxPlan selected;
    if (RADIO_MODEL_Plan(frequency_hz, seekOrigin.parameters,
                         RECEIVER_Capabilities(), &selected) == RX_OK &&
        selected.device == seekOrigin.device &&
        SEEK_Accept(&(Seek){.active=true, .origin_hz=seek.origin_hz,
                             .lower_hz=seek.lower_hz,.end_hz=seek.end_hz,
                             .wrapped=seek.wrapped}, selected.frequency_hz)) {
      apply(&selected); // The only place a found station is saved.
      return;
    }
    status = "Seek failed";
  }
  // Restoring directly does not update RadioUserState or request a save.
  if (!RECEIVER_Apply(&seekOrigin)) {
    RX_RuntimeStop(&runtime, stopReceiver, NULL);
    notify("RX error");
  } else if (status) {
    notify(status);
  }
}

void startSeek(void) {
  if (!pauseWatch()) return;
  if (activeSlot.source == RADIO_SOURCE_CHANNEL) {
    notify("VFO only");
    return;
  }
  if (!runtime.active || activeSlot.active_band >= radioBandCount) {
    notify("RX unavailable");
    return;
  }
  if (activeSlot.current.parameters.mode == RX_LSB ||
      activeSlot.current.parameters.mode == RX_USB) {
    notify("SSB seek off");
    return;
  }
  seekOrigin = runtime.current;
  if (!SEEK_Start(&seek, &radioBands[activeSlot.active_band],
                  seekOrigin.frequency_hz, seekOrigin.parameters.step_hz)) {
    notify("Seek failed");
    return;
  }
  seekNative = RX_DEVICE_SeekNative(&seekOrigin);
  seekPhase = seekNative ? SEEK_NATIVE_START : SEEK_STEP;
  seekAt = Now();
  seekDeadline = Now() + 15000;
  RECEIVER_Mute();
  resetRds();
  message = NULL;
  gRedrawScreen = true;
}

void seekUpdate(void) {
  if (!seek.active) return;
  uint32_t now = Now();
  if (seekPhase == SEEK_STEP) {
    uint32_t next;
    if (!SEEK_Next(&seek, &next)) {
      finishSeek(false, 0, "No station");
      return;
    }
    RxPlan candidate;
    RadioParameters params = seekOrigin.parameters;
    if (params.squelch < 4 && seekOrigin.device == RX_DEVICE_BK)
      params.squelch = 4; // Monitor/SQ0 must not match every noise carrier.
    if (RADIO_MODEL_Plan(next, params, RECEIVER_Capabilities(),
                         &candidate) != RX_OK ||
        candidate.device != seekOrigin.device ||
        candidate.frequency_hz < seek.lower_hz ||
        candidate.frequency_hz >= seek.end_hz)
      return;
    if (!RECEIVER_Apply(&candidate)) {
      finishSeek(false, 0, "Seek failed");
      return;
    }
    seekAt = now;
    seekPhase = SEEK_SETTLE;
  } else if (seekPhase == SEEK_SETTLE || seekPhase == SEEK_VERIFY ||
             seekPhase == SEEK_WRAP_SETTLE) {
    uint32_t elapsed = now - seekAt;
    uint32_t wait = seekPhase == SEEK_VERIFY ? 80u : 180u;
    if (elapsed < wait) return;
    bool detected;
    if (!RX_DEVICE_SeekSignal(seekOrigin.device, &detected)) {
      finishSeek(false, 0, "Seek failed");
      return;
    }
    if (detected && seekPhase == SEEK_SETTLE) {
      seekPhase = SEEK_VERIFY;
      seekAt = now;
    } else if (detected) {
      finishSeek(true, seek.cursor_hz, NULL);
    } else if (seekPhase == SEEK_WRAP_SETTLE) {
      seekPhase = SEEK_NATIVE_START;
    } else {
      seekPhase = SEEK_STEP;
    }
  } else if (seekPhase == SEEK_NATIVE_START) {
    // The SI hardware cannot search over arbitrary VFO modes or spacings.
    uint32_t end = seek.wrapped ? seek.origin_hz : seek.end_hz-1u;
    if (!RX_DEVICE_SeekStart(&seekOrigin, seek.lower_hz, end)) {
      finishSeek(false, 0, "Seek failed");
      return;
    }
    seekDeadline = now + 15000u;
    seekAt = now;
    seekPhase = SEEK_NATIVE_POLL;
  } else if (seekPhase == SEEK_NATIVE_POLL) {
    if ((int32_t)(now-seekDeadline) >= 0) {
      finishSeek(false, 0, "No station");
      return;
    }
    if (now-seekAt < 90u) return;
    seekAt = now;
    uint32_t frequency = 0;
    RxSeekResult result = RX_DEVICE_SeekPoll(seekOrigin.device, &frequency);
    if (result == RX_SEEK_BUSY) return;
    if (result == RX_SEEK_ERROR) {
      finishSeek(false, 0, "Seek failed");
      return;
    }
    if (result == RX_SEEK_FOUND && SEEK_Accept(&seek, frequency)) {
      finishSeek(true, frequency, NULL);
      return;
    }
    // A complete hardware segment ended; the application owns the one wrap.
    if (seek.wrapped || !SEEK_Wrap(&seek)) {
      finishSeek(false, 0, "No station");
      return;
    }
    RX_DEVICE_SeekCancel(seekOrigin.device);
    RxPlan first;
    if (RADIO_MODEL_Plan(seek.lower_hz, seekOrigin.parameters,
                         RECEIVER_Capabilities(), &first) != RX_OK ||
        first.device != seekOrigin.device ||
        !RECEIVER_Apply(&first)) {
      finishSeek(false, 0, "Seek failed");
      return;
    }
    seek.cursor_hz = first.frequency_hz;
    seekAt = now;
    seekPhase = SEEK_WRAP_SETTLE; // Evaluate the first channel once.
  }
  gRedrawScreen = true;
}
