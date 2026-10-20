#include "drivers/bk48x9/bk48x9.h"

// Both BK4819 and the K1 BK4829 references use the same scan registers.
void BK48X9_StartToneScan(void) {
  BK48X9_Idle();
  BK48X9_WriteRegister(BK48X9_REG_51, 0x0300); // Auto CSS bandwidth off, TX CSS off.
  BK48X9_RX_TurnOn();
}
BK48X9_CssScanResult_t BK48X9_ReadToneScan(uint32_t *value) {
  uint16_t high = BK48X9_ReadRegister(BK48X9_REG_69);
  if (!(high & 0x8000u)) {
    uint16_t low = BK48X9_ReadRegister(BK48X9_REG_6A);
    if (high != BK48X9_ReadRegister(BK48X9_REG_69)) return BK48X9_CSS_RESULT_NOT_FOUND;
    *value = ((uint32_t)(high & 0xFFFu) << 12) | (low & 0xFFFu);
    return BK48X9_CSS_RESULT_CDCSS;
  }
  uint16_t raw = BK48X9_ReadRegister(BK48X9_REG_68);
  if (raw & 0x8000u) return BK48X9_CSS_RESULT_NOT_FOUND;
  *value = (raw & 0x1FFFu)*4843u/10000u; // 26 MHz reference -> Hz*10.
  return BK48X9_CSS_RESULT_CTCSS;
}
