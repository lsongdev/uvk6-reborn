# FM audio periodic noise investigation (2026-10-20)

## Report and scope

On the modified UV-K6 with SI4732 analog FM broadcast reception, speech
sounds clear but a faint periodic "da-da" / tapping remains in the background.
This is a **reported listening symptom**, not yet recorded with an oscilloscope
or proven to be a firmware defect. Audio comes from SI4732's analog output;
the MCU does not decode a PCM stream. Periodic I2C/GPIO/SPI activity could
couple electrically into the RF/analog audio path, or intermittent I2C failures
could cause actual audio-gate changes. Both can sound like clicks.

The default K6 board configuration selects SI4732; a stock BK1080 variant or
the BK4829-based UV-K1 follows a different hardware path. This change does not
alter transmit/RF calibrations, AGC or undocumented FM tuning properties.

## Evidence and code audit

* `src/radio/watch.c` previously called `RECEIVER_Measure()` **on every
  main-loop iteration** whenever a watch source owned the audio. When an
  SI4732 broadcast is the watch background, this repeatedly invokes
  `RSQ_GET()` (I2C `FM_RSQ_STATUS`). This is an actual unbounded polling
  defect independent of the presence of audible interference.
* `src/app/loop.c` sampled ordinary squelch every 50 ms. Even for SI4732
  WFM `SQ0` (continuous/open receive), `RX_DEVICE_Sample()` read RSQ before
  returning `open=true`. SQ0 audio gating does not need that data.
* `src/radio/receiver.c` ignored the success return from `RX_DEVICE_Sample()`.
  One I2C read failure was interpreted as closed squelch, potentially causing
  `RECEIVER_SetOpen(false)` and switching the audio route/volume. This is a
  confirmed logic bug, though no real NACK-correlated click has been measured.
* RDS drains up to four FIFO entries per 100 ms using SI4732 I2C. The
  keyboard scans every 14 ms; PA10 and PA11 are shared between keyboard and
  SI4732/EEPROM I2C. LCD SPI and PWM backlight are further plausible electrical
  sources; no causal source has yet been isolated.
* `losehu/uv-k5-firmware-custom` has an `SNR_flag` that controls `RSQ_GET()`
  in `app/si.c`. Its release notes discuss periodic noise and disabling the
  SI4732 SNR display. This supports testing RSQ activity first, but does not
  establish that our board has the same mechanism.
  References:
  https://github.com/losehu/uv-k5-firmware-custom/releases
  https://github.com/losehu/uv-k5-firmware-custom/blob/main/app/si.c
* `fagci/hawk5` (our upstream) also uses SI4732 RDS/RSQ via I2C. Its
  RDS FIFO threshold differs from ours (12 vs 1); this difference alone is
  not evidence of a protocol bug or noise source.
  https://github.com/fagci/hawk5/blob/main/src/driver/si473x.c
* Other SI47xx devices have reported possible display/backlight coupled noise:
  https://github.com/esp32-si4732/ats-mini/discussions/293

## Changes in this PR

1. One sampling policy (`src/radio/receiver.h`, `RECEIVER_Poll`): a plan
   with SQ0 and no receive tone stays open and is sampled only for the
   meter, every `RECEIVER_MeterPeriodMs()` (SI4732 500 ms, BK1080 1000 ms,
   BK 200 ms); other plans sample squelch every 50 ms (BK1080 1000 ms).
   Watch uses the same meter period instead of measuring every loop.
2. The BK1080 is read once a second whatever its squelch, so the signal
   meter keeps updating.
3. Failed `RX_DEVICE_Sample()` leaves the current audio gate and last valid
   signal meter value intact. This prevents one failed read being interpreted
   as "squelch closed"; a separate policy for sustained hardware faults may
   be implemented after real-device evidence.
4. Build-time diagnostic controls permit independent isolation of RSQ and RDS
   periodic accesses. They default to normal (RDS enabled, slower RSQ), and
   do not change EEPROM configuration or persistent user settings.
5. Host tests cover a transient sample failure and bounded watch-meter reads.

## A/B test matrix

Use the **same strong FM station, volume, antenna position, supply, speaker
and listening position**. Start with one slot and SQ0, then repeat with
SI4732 broadcast background + BK event watch. Do not compare raw audio
loudness across different settings or against an actively transmitting radio.

| Build | Command | What it isolates |
| --- | --- | --- |
| Default | `make -B all BOARD=uvk6` | 500 ms SI RSQ meter + RDS |
| No periodic SI FM RSQ | `make -B all BOARD=uvk6 FM_RSQ_POLL_MS=0` | RSQ meter access in SQ0 and watch background |
| No RDS polling | `make -B all BOARD=uvk6 ENABLE_RDS_POLL=0` | RDS FIFO I2C transactions (initial RDS setup may still execute) |
| Both off | `make -B all BOARD=uvk6 FM_RSQ_POLL_MS=0 ENABLE_RDS_POLL=0` | Residual keyboard, LCD, backlight, ADC and RF/hardware effects |

Makefile tracks both new flags in `build-flags`, so changing a value
invalidates old objects. For actual flashing, use the project's normal
backup/flashing workflow in `docs/development.md`; **do not flash a BK1080
build to the SI4732-modified radio**.

Record: firmware commit, active slot(s), chosen WFM/SQ level, station, RDS
available or not, approximate tap interval (e.g. 50/100/500 ms), whether
speaker versus headphones differ, and whether an RX LED toggles with each
sound. Useful additional experiments after the above: freeze LCD refresh,
use a fixed (non-PWM) backlight level via temporary board-only diagnostic
code, suspend matrix scanning while maintaining physical PTT safety, or
measure VDD/audio with an oscilloscope. These are **not** changes made here.

## UV-K1 hardware evidence (2026-10-20)

On the UV-K1 (BK1080 broadcast over a bit-banged I2C on PF5/PF6) the user
heard a faint regular ticking with BK1080 read every 50 ms. Reading it once
a second instead left a barely audible tick about once a second. Bus traffic
to the receiver chip is therefore an audible source on that radio; the K6
SI4732 path has not been A/B tested yet.

## Verification limits

Host mocks prove logic and bounded polling, not physical RF/AF behavior.
Only listening and/or analog measurements can establish which noise source
dominates on this particular unit. Do not treat quieter output under a
diagnostic flag as proof of a defective SI4732 chip; it may be digital
supply or ground coupling, board routing, or firmware-induced gating.
