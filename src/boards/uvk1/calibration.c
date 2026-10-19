#include "hal/board.h"
#include "store/calibration.h"

bool BOARD_TxCalibration(uint32_t hz, BoardTxCalibration *out) {
  if (!out || hz % 10u) return false;
  // K1 factory bands from reference frequencies.c/radio.c (5fedc09).
  // Below 137 MHz clamp to band 2's first anchor; band 1 is an RX band
  // with placeholder PA data, not the VHF transmitter's calibration.
  if (hz >= 136000000u && hz < 174000000u)
    *out = (BoardTxCalibration){CALIBRATION_PA_OFFSET + 2u * CALIBRATION_PA_STRIDE,
                               137000000u, 155500000u, 174000000u};
  else if (hz >= 400000000u && hz < 470000000u)
    *out = (BoardTxCalibration){CALIBRATION_PA_OFFSET + 5u * CALIBRATION_PA_STRIDE,
                               400000000u, 435000000u, 470000000u};
  else return false;
  return true;
}

// K1 factory RX layout and reference squelch tuning (5fedc09).
#include "hal/storage.h"
bool BOARD_RxCalibration(BoardRxCalibration *out) {
  if (!out) return false;
  *out=(BoardRxCalibration){0,58,8};
  uint8_t misc[CALIBRATION_RX_MISC_BYTES];
  if (STORAGE_Read(STORAGE_CALIBRATION,CALIBRATION_RX_MISC_OFFSET,misc,sizeof(misc))) {
    bool erased=true;
    for (unsigned i=0;i<sizeof(misc);++i) erased &= misc[i]==0xFF;
    if (!erased) {
      out->trim=(int16_t)((uint16_t)misc[0] | ((uint16_t)misc[1]<<8));
      out->volume=misc[6];out->dac=misc[7];
    }
  }
  return true;
}
uint8_t BOARD_RxCalibrationBand(uint32_t hz) { return hz<174000000u ? 0 : 1; }
bool BOARD_SquelchCalibration(uint32_t hz,uint8_t level,BoardSquelchCalibration *out) {
  if (!level || !out) return false;
  if (level>9) level=9;
  uint8_t row[6];
  uint32_t base=(BOARD_RxCalibrationBand(hz) ? CALIBRATION_SQUELCH_UHF_OFFSET : CALIBRATION_SQUELCH_VHF_OFFSET)+level;
  for (uint8_t i=0;i<6;++i)
    if (!STORAGE_Read(STORAGE_CALIBRATION,base+16u*i,&row[i],1) || row[i]==0xFF) return false;
  uint16_t ro=row[0]/2u,rc=row[1],no=row[2]*2u,nc=row[3],go=row[5]*2u,gc=row[4];
  if (rc==ro && rc>=2) rc-=2;
  if (nc==no && nc<=125) nc+=2;
  if (gc==go && gc<=253) gc+=2;
  *out=(BoardSquelchCalibration){ro,rc,no>127 ? 127 : no,nc>127 ? 127 : nc,go>255 ? 255 : go,gc};
  return true;
}
