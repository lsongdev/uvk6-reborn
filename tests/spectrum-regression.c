#include "rf-ranges.h"
// The spectrum window: steps, sweep bookkeeping, peaks, panning and zoom.
#include "core/spectrum.h"
#include <assert.h>

int main(void) {
  Spectrum s;
  SPECTRUM_Start(&s, 145500000, 12500,testBk4819Ranges);
  assert(s.step_hz == 12500 && s.cursor == SPECTRUM_CENTER);
  assert(SPECTRUM_Frequency(&s, 0) == 144700000 && SPECTRUM_Frequency(&s, 64) == 145500000);
  SPECTRUM_Start(&s, 145500000, 8330,testBk4819Ranges); // Airband 8.33k rounds up.
  assert(s.step_hz == 12500);
  SPECTRUM_Start(&s, 98000000, 9000000,testBk4819Ranges); // Coarser than the list: widest.
  assert(s.step_hz == 500000);
  SPECTRUM_Start(&s, 200000, 6250,testBk4819Ranges); // The window clips at 0 Hz.
  assert(SPECTRUM_Frequency(&s, 0) == 0);

  SPECTRUM_Start(&s, 145500000, 12500,testBk4819Ranges);
  for (unsigned i = 0; i + 1 < SPECTRUM_POINTS; ++i) assert(!SPECTRUM_Record(&s, 40));
  assert(SPECTRUM_Record(&s, 90) && s.next == 0);
  assert(SPECTRUM_Floor(&s) == 40 && s.peak[127] == 90);
  for (unsigned i = 0; i < SPECTRUM_POINTS; ++i) SPECTRUM_Record(&s, 40);
  assert(s.peak[127] == 89 && s.level[127] == 40); // Peaks fall 1 dB a sweep.

  // Panning slides the measured points and the point in flight with them.
  s.cursor = SPECTRUM_POINTS - 1; s.next = 10; s.level[127] = 77;
  SPECTRUM_Move(&s, true);
  assert(s.center_hz == 145512500 && s.cursor == 127 && s.next == 9);
  assert(s.level[126] == 77 && s.level[127] == 0);
  s.cursor = 0; s.next = 127;
  SPECTRUM_Move(&s, false);
  assert(s.center_hz == 145500000 && s.next == 0 && s.level[127] == 77 && s.level[0] == 0);
  SPECTRUM_Move(&s, true); assert(s.cursor == 1);

  // Zoom centres on the cursor and restarts the sweep.
  assert(SPECTRUM_Zoom(&s, true));
  assert(s.step_hz == 25000 && s.center_hz == 144712500 && s.cursor == 64 && !s.level[0]);
  assert(SPECTRUM_Zoom(&s, false) && SPECTRUM_Zoom(&s, false) && s.step_hz == 6250);
  assert(!SPECTRUM_Zoom(&s, false));
  return 0;
}
