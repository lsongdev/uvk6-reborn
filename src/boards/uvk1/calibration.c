#include "hal/board.h"
#include "store/calibration.h"

bool BOARD_TxCalibration(uint32_t hz, BoardTxCalibration *out) {
  if (!out || hz % 10u) return false;
  // Original (non-WIDE_RX) factory bands in reference 5fedc09. Expanding
  // tuning must not stretch these calibration anchors to the new limits.
  static const BoardTxCalibration bands[CALIBRATION_PA_BANDS] = {
    {0xD0u, 50000000u, 63000000u, 76000000u},
    {0xE0u,108000000u,122500000u,137000000u},
    {0xF0u,137000000u,155500000u,174000000u},
    {0x100u,174000000u,262000000u,350000000u},
    {0x110u,350000000u,375000000u,400000000u},
    {0x120u,400000000u,435000000u,470000000u},
    {0x130u,470000000u,535000000u,600000000u},
  };
  uint8_t band=CALIBRATION_PA_BANDS-1u;
  // Keep the previously used VHF record at 136--137 MHz.
  while (band && hz<(band==2 ? 136000000u : bands[band].lower_hz)) --band;
  *out=bands[band];
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
