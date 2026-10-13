#ifndef CORE_CAPTURE_H
#define CORE_CAPTURE_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { CAPTURE_AUTO, CAPTURE_VHF, CAPTURE_UHF } CaptureScope;
typedef enum { CAPTURE_RANGE_VHF, CAPTURE_RANGE_UHF } CaptureRange;
typedef enum { CAPTURE_BUSY, CAPTURE_INVALID, CAPTURE_SAMPLE } CaptureSample;
typedef enum { CAPTURE_WAIT, CAPTURE_RESTART, CAPTURE_FOUND, CAPTURE_TIMEOUT } CaptureAction;
typedef struct {
  uint32_t started_at, attempt_at, last_hz;
  CaptureScope scope;
  CaptureRange range;
  uint8_t hits, attempts;
} CaptureRuntime;

void CAPTURE_Start(CaptureRuntime *runtime, CaptureScope scope, uint32_t now);
CaptureAction CAPTURE_Update(CaptureRuntime *runtime, uint32_t now,
                             CaptureSample sample, uint32_t frequency_hz);
#endif
