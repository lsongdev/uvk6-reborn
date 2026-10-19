#include "drivers/bk4819/bk4819.h"
#include <assert.h>
#include "hal/board.h"
BoardRadioChip BOARD_RadioChip(void) { return BOARD_RADIO_BK4819; }

static uint16_t registers[128];
static struct { uint8_t reg; uint16_t value; } writes[16];
static unsigned count;
uint16_t BK4819_ReadRegister(BK4819_REGISTER_t reg) { return registers[reg]; }
void BK4819_WriteRegister(BK4819_REGISTER_t reg, uint16_t value) {
  assert(count < 16);
  writes[count].reg = reg; writes[count++].value = value;
  registers[reg] = value;
}
void BK4819_SetAF(BK4819_AF_Type_t af) {
  BK4819_WriteRegister(BK4819_REG_47,0x6040u | ((uint16_t)af << 8));
}
void BK4819_SetRegValue(RegisterSpec spec, uint16_t value) {
  uint16_t reg = BK4819_ReadRegister(spec.num);
  BK4819_WriteRegister(spec.num,(reg & ~(spec.mask << spec.offset)) | (value << spec.offset));
}
void BK4819_XtalSet(XtalMode mode) {
  assert(mode == XTAL_2_26M);
  BK4819_WriteRegister(0x3C,20360);BK4819_WriteRegister(0x3D,0x2AAB);
}
static void check(bool am) {
  count=0; registers[0x31]=0xBEEF;registers[0x73]=0xFFFF;
  BK4819_SetRxMode(am);
  const uint8_t order[]={0x47,0x73,0x3C,0x3D,0x31,0x42,0x2A,0x2B,0x2F};
  const uint16_t fm[]={0x6140,0xFFEF,20360,0x2AAB,0xBEEE,0x6B5A,0x7400,0,0x9890};
  const uint16_t ax[]={0x6140,0xFFEF,20360,0x2AAB,0xBEEF,0x6F5C,0x7434,0x0600,0x9990};
  assert(count==sizeof(order));
  for (unsigned i=0;i<count;++i) {
    assert(writes[i].reg==order[i]);assert(writes[i].value==(am ? ax[i] : fm[i]));
  }
  count=0;BK4819_SetRxMode(am);assert(count==0);
}
int main(void) {
  check(false);check(true);check(false);
  BK4819_InvalidateRxMode();check(false);
  BK4819_InvalidateRxMode();check(true);
  return 0;
}
