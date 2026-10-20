#include "hal/board.h"
#include "store/calibration.h"
#include "hal/storage.h"

bool BOARD_TxCalibration(uint32_t hz, BoardTxCalibration *out) {
  if (!out || hz % 10u) return false;
  // Seven factory records, selected by frequency rather than a TX whitelist.
  // Preserve the existing K6 VHF/UHF anchors. Outside a record's anchors,
  // the shared interpolator clamps to its first/last value.
  static const BoardTxCalibration bands[CALIBRATION_PA_BANDS] = {
    {0xD0u, 50000000u, 65000000u, 76000000u},
    {0xE0u,108000000u,122000000u,135999900u},
    {0xF0u,136000000u,150000000u,173999990u},
    {0x100u,174000000u,260000000u,349999900u},
    {0x110u,350000000u,370000000u,399999900u},
    {0x120u,400000000u,435000000u,469999990u},
    {0x130u,470000000u,550000000u,600000000u},
  };
  uint8_t band=CALIBRATION_PA_BANDS-1u;
  while (band && hz<bands[band].lower_hz) --band;
  *out=bands[band];
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
