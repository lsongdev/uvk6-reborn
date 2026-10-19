#include "hal/board.h"
#include "store/calibration.h"

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

bool BOARD_RxCalibration(BoardRxCalibration *out) { (void)out; return false; }
uint8_t BOARD_RxCalibrationBand(uint32_t hz) { (void)hz; return 0; }
bool BOARD_SquelchCalibration(uint32_t hz,uint8_t level,BoardSquelchCalibration *out) {
  (void)hz;(void)level;(void)out;return false;
}
