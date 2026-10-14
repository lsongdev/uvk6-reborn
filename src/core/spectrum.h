#ifndef CORE_SPECTRUM_H
#define CORE_SPECTRUM_H
#include <stdbool.h>
#include <stdint.h>

// Set by the Makefile (ENABLE_SPECTRUM); host tests keep the spectrum.
#ifndef UVK6_ENABLE_SPECTRUM
#define UVK6_ENABLE_SPECTRUM 1
#endif

// A sweep of SPECTRUM_POINTS frequencies, step_hz apart, centred on
// center_hz. Levels are RSSI in dB above the chip's zero (raw RSSI / 2);
// 0 means not measured (outside the chip's range, or not yet swept).
#define SPECTRUM_POINTS 128u
#define SPECTRUM_CENTER (SPECTRUM_POINTS / 2u)
typedef struct {
  uint32_t center_hz, step_hz;
  uint8_t level[SPECTRUM_POINTS];
  uint8_t peak[SPECTRUM_POINTS]; // Falls 1 dB per sweep towards the level.
  uint8_t next;   // Point being measured.
  uint8_t cursor; // Point the user points at.
} Spectrum;

void SPECTRUM_Start(Spectrum *s, uint32_t center_hz, uint32_t step_hz);
uint32_t SPECTRUM_Frequency(const Spectrum *s, uint8_t point);
// Stores the measurement of point `next` and moves on; true when the sweep
// just completed.
bool SPECTRUM_Record(Spectrum *s, uint8_t level);
// Cursor moves one point; at an edge the window pans one step instead.
void SPECTRUM_Move(Spectrum *s, bool up);
// Next step size up (wider) or down (narrower), centred on the cursor.
bool SPECTRUM_Zoom(Spectrum *s, bool wider);
// Lowest measured level of the last sweep (the noise floor), 0 if none.
uint8_t SPECTRUM_Floor(const Spectrum *s);
#endif
