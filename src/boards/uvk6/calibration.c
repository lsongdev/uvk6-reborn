#include "hal/board.h"
#include "store/calibration.h"
#include "hal/storage.h"

bool BOARD_TxCalibration(uint32_t hz, BoardTxCalibration *out) {
  if (!out || hz % 10u) return false;
  // Preserve the K6's existing calibration interpolation and TX limits.
  if (hz >= 136000000u && hz < 174000000u)
    *out = (BoardTxCalibration){CALIBRATION_PA_OFFSET + 2u * CALIBRATION_PA_STRIDE,
                               136000000u, 150000000u, 173999990u};
  else if (hz >= 400000000u && hz < 470000000u)
    *out = (BoardTxCalibration){CALIBRATION_PA_OFFSET + 5u * CALIBRATION_PA_STRIDE,
                               400000000u, 435000000u, 469999990u};
  else return false;
  return true;
}

bool BOARD_RxCalibration(BoardRxCalibration *out) {
  if (!out) return false;
  // REG_3B trims the shared RX/TX reference, not an offset in Hz.
  // Keep the current K6 AF gains; this change only restores crystal trim.
  *out = (BoardRxCalibration){0,58,8};
  uint8_t misc[CALIBRATION_RX_MISC_BYTES];
  if (STORAGE_Read(STORAGE_CALIBRATION,CALIBRATION_RX_MISC_OFFSET,misc,sizeof(misc))) {
    bool erased = true;
    for (unsigned i = 0; i < sizeof(misc); ++i) erased &= misc[i] == 0xFF;
    if (!erased) {
      int16_t trim = (int16_t)((uint16_t)misc[0] | ((uint16_t)misc[1] << 8));
      if (trim >= -1000 && trim <= 1000) out->trim = trim;
    }
  }
  return true;
}
uint8_t BOARD_RxCalibrationBand(uint32_t hz) { (void)hz; return 0; }
bool BOARD_SquelchCalibration(uint32_t hz,uint8_t level,BoardSquelchCalibration *out) {
  (void)hz;(void)level;(void)out;return false;
}
