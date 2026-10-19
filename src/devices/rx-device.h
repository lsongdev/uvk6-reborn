#ifndef RX_DEVICE_H
#define RX_DEVICE_H

#include "core/radio-model.h"
#include "core/rds-receiver.h"
#include "core/capture.h"
#include "core/spectrum.h"

// Apply an already validated receive plan once. This is the sole hardware
// tuning path; callers own the user state and failed-plan recovery.
// Probes the broadcast chips once; afterwards returns the cached result.
ReceiverCapabilities RX_DEVICE_Capabilities(void);
// BK4819 from the stored preferences, PA off, broadcast chip parked.
void RX_DEVICE_Init(void);
bool RX_DEVICE_SiActive(void); // SI4732 powered (status line).
bool RX_DEVICE_Apply(const RxPlan *plan, RxDevice previous);
bool RX_DEVICE_ApplyConcurrent(const RxPlan *plan); // Configure muted, keep other chip alive.
bool RX_DEVICE_SelectAudio(uint8_t devices, RxDevice selected, bool open);
// Speaker gate for the tuned chip: break-before-make with a settle delay.
void RX_DEVICE_SetAudio(RxDevice device, bool open);
// One RSSI/squelch sample of the tuned chip. Squelch includes the NFM tone.
// Returns false when the chip did not answer (then *open is false).
bool RX_DEVICE_Sample(const RxPlan *plan, uint16_t *rssi, bool *open);
// Signal strength 0..14 from a sampled RSSI: approximate S units on BK4819,
// relative strength on the broadcast chips.
uint8_t RX_DEVICE_SignalLevel(const RxPlan *plan, uint16_t rssi);
void RX_DEVICE_StopChip(RxDevice device);
bool RX_DEVICE_WatchSignal(const RxPlan *plan, bool *found);
bool RX_DEVICE_ReadRDS(RdsPacket *packet, bool clear_fifo);

typedef enum { RX_SEEK_BUSY, RX_SEEK_FOUND, RX_SEEK_LIMIT, RX_SEEK_ERROR } RxSeekResult;
bool RX_DEVICE_SeekNative(const RxPlan *plan);
bool RX_DEVICE_SeekStart(const RxPlan *plan, uint32_t lower_hz,
                         uint32_t upper_hz);
RxSeekResult RX_DEVICE_SeekPoll(RxDevice device, uint32_t *frequency_hz);
void RX_DEVICE_SeekCancel(RxDevice device);
bool RX_DEVICE_SeekSignal(RxDevice device, bool *found);

// Capture temporarily owns BK with its audio muted; previous is the chip
// to leave (pass RX_DEVICE_BK to keep a broadcast chip playing). Stop before
// any RX apply.
void RX_DEVICE_CaptureStart(CaptureRange range, RxDevice previous);
CaptureSample RX_DEVICE_CapturePoll(CaptureRange range, uint32_t *frequency_hz);
void RX_DEVICE_CaptureStop(void);
void RX_DEVICE_InvalidateBk(void);

// Spectrum borrows BK muted, like capture: previous is the chip to leave.
// Tune one point, wait at least 1 ms, then read its RSSI (raw 0.5 dB units).
void RX_DEVICE_SpectrumStart(RxDevice previous);
void RX_DEVICE_SpectrumTune(uint32_t frequency_hz);
uint16_t RX_DEVICE_SpectrumRssi(void);
void RX_DEVICE_SpectrumStop(void);

#endif
