#ifndef UI_SPECTRUM_H
#define UI_SPECTRUM_H
#include "core/spectrum.h"
// Bars from the noise floor up, 2 dB per pixel, one column per point;
// dots hold the peaks, a dotted line marks the cursor and a tick at the
// top the point being measured. Keeps no state.
void UI_SpectrumRender(uint8_t y, uint8_t height, const Spectrum *spectrum);
#endif
