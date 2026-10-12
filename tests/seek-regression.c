#include "core/seek.h"
#include <assert.h>

int main(void) {
  RadioBand band = {.start_hz = 100000000, .end_hz = 100500000};
  Seek seek;
  uint32_t next;
  assert(SEEK_Start(&seek, &band, 100200000, 100000));
  assert(SEEK_Next(&seek, &next) && next == 100300000);
  assert(SEEK_Next(&seek, &next) && next == 100400000);
  assert(SEEK_Next(&seek, &next) && next == 100000000 && seek.wrapped);
  assert(SEEK_Next(&seek, &next) && next == 100100000);
  assert(!SEEK_Next(&seek, &next) && !seek.active);
  assert(!SEEK_Start(&seek, &band, 99900000, 100000));
  assert(!SEEK_Start(&seek, &band, 100200000, 0));
  assert(SEEK_Start(&seek, &band, 100000000, 100000));
  for (unsigned n=0;n<4;++n)
    assert(SEEK_Next(&seek, &next));
  assert(!SEEK_Next(&seek, &next)); // Starting at lower bound stops at wrap.
  assert(SEEK_Start(&seek, &band, 100250000, 100000));
  assert(SEEK_Wrap(&seek));
  assert(SEEK_Accept(&seek, 100100000));
  assert(!SEEK_Accept(&seek, 100300000));
  assert(!SEEK_Wrap(&seek));
  assert(!seek.active);
  // The selected band's bounds win, even if a narrower band overlaps.
  band = (RadioBand){.start_hz = 64000000, .end_hz = 108000001};
  assert(SEEK_Start(&seek, &band, 107900000, 100000));
  assert(SEEK_Next(&seek, &next) && next == 108000000);
  assert(SEEK_Next(&seek, &next) && next == 64000000);
  return 0;
}
