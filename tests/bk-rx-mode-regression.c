#include "drivers/bk48x9/bk48x9.h"
#include <assert.h>

static uint16_t registers[128];
static struct { uint8_t reg; uint16_t value; } writes[16];
static unsigned count;
uint16_t BK48X9_ReadRegister(BK48X9_REGISTER_t reg) { return registers[reg]; }
void BK48X9_WriteRegister(BK48X9_REGISTER_t reg, uint16_t value) {
  assert(count < 16);
  writes[count].reg = reg; writes[count++].value = value;
  registers[reg] = value;
}
static void check(bool am) {
  count=0; registers[0x47]=0x6040; registers[0x31]=0xBEEF;registers[0x73]=0xFFFF;
  BK48X9_SetRxMode(am);
  const uint8_t order[]={0x73,0x3C,0x3D,0x31,0x42,0x2A,0x2B,0x2F};
  const uint16_t fm[]={0xFFEF,20360,0x2AAB,0xBEEE,0x6B5A,0x7400,0,0x9890};
  const uint16_t ax[]={0xFFEF,20360,0x2AAB,0xBEEF,0x6F5C,0x7434,0x0600,0x9990};
  assert(registers[0x47] == 0x6040); // Configuration must keep audio muted.
  assert(count==sizeof(order));
  for (unsigned i=0;i<count;++i) {
    assert(writes[i].reg==order[i]);assert(writes[i].value==(am ? ax[i] : fm[i]));
  }
  count=0;BK48X9_SetRxMode(am);assert(count==0);
}
int main(void) {
  check(false);check(true);check(false);
  BK48X9_InvalidateRxMode();check(false);
  BK48X9_InvalidateRxMode();check(true);
  return 0;
}
