#include "core/capture.h"
#include <assert.h>
#include <limits.h>

int main(void) {
  CaptureRuntime r;
  CAPTURE_Start(&r,CAPTURE_AUTO,0);
  assert(CAPTURE_Update(&r,499,CAPTURE_BUSY,0)==CAPTURE_WAIT);
  assert(CAPTURE_Update(&r,500,CAPTURE_BUSY,0)==CAPTURE_RESTART);
  assert(r.range==CAPTURE_RANGE_VHF && !r.hits);
  assert(CAPTURE_Update(&r,1000,CAPTURE_BUSY,0)==CAPTURE_RESTART);
  assert(r.range==CAPTURE_RANGE_UHF && !r.hits);
  assert(CAPTURE_Update(&r,1200,CAPTURE_SAMPLE,438500000)==CAPTURE_RESTART);
  assert(CAPTURE_Update(&r,1400,CAPTURE_SAMPLE,438503000)==CAPTURE_FOUND);
  assert(r.last_hz==438503000);
  CAPTURE_Start(&r,CAPTURE_VHF,0);
  assert(CAPTURE_Update(&r,200,CAPTURE_SAMPLE,145500000)==CAPTURE_RESTART);
  assert(CAPTURE_Update(&r,400,CAPTURE_SAMPLE,145503001)==CAPTURE_RESTART);
  assert(r.hits==1);
  assert(CAPTURE_Update(&r,600,CAPTURE_INVALID,0)==CAPTURE_RESTART && !r.hits);
  assert(CAPTURE_Update(&r,800,CAPTURE_SAMPLE,145503001)==CAPTURE_RESTART);
  assert(CAPTURE_Update(&r,1000,CAPTURE_SAMPLE,145500001)==CAPTURE_FOUND);
  // Cross-range hits must not combine, including implausible identical samples.
  CAPTURE_Start(&r,CAPTURE_AUTO,0);
  CAPTURE_Update(&r,500,CAPTURE_BUSY,0);
  CAPTURE_Update(&r,700,CAPTURE_SAMPLE,145500000);
  assert(r.range==CAPTURE_RANGE_UHF && !r.hits);
  assert(CAPTURE_Update(&r,900,CAPTURE_SAMPLE,145500000)==CAPTURE_RESTART);
  CAPTURE_Start(&r,CAPTURE_UHF,UINT32_MAX-100);
  assert(r.range==CAPTURE_RANGE_UHF);
  assert(CAPTURE_Update(&r,398,CAPTURE_BUSY,0)==CAPTURE_WAIT);
  assert(CAPTURE_Update(&r,399,CAPTURE_BUSY,0)==CAPTURE_RESTART);
  // No session limit: a minute in, detection still restarts and confirms.
  assert(CAPTURE_Update(&r,60000,CAPTURE_BUSY,0)==CAPTURE_RESTART);
  assert(CAPTURE_Update(&r,60100,CAPTURE_SAMPLE,438500000)==CAPTURE_RESTART);
  assert(CAPTURE_Update(&r,60300,CAPTURE_SAMPLE,438501000)==CAPTURE_FOUND);
  return 0;
}
