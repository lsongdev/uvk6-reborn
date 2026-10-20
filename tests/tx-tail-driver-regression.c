// Both actual RF backends. During the tail only mic/CSS registers may change;
// PA stays on until the caller explicitly ends the RF phase.
#include "drivers/bk48x9/bk48x9.h"
#include "core/tone.h"
#include <assert.h>
static uint16_t regs[128];
static unsigned tailWrites;
static bool inTail;
void BK48X9_BusInit(void) {}
void SYS_DelayMs(uint32_t ms) { (void)ms; }
uint16_t BK48X9_ReadRegister(BK48X9_REGISTER_t reg) { return reg==0x0C ? 0 : regs[reg]; }
void BK48X9_WriteRegister(BK48X9_REGISTER_t reg, uint16_t value) {
  if (inTail) {
    assert(reg==BK48X9_REG_50 || reg==BK48X9_REG_51 || reg==BK48X9_REG_52);
    assert(regs[0x33] & 0x20); // No premature PA gate off.
    if (!tailWrites) assert(reg==BK48X9_REG_50 && (value & 0x8000));
    ++tailWrites;
  }
  regs[reg]=value;
}
int main(void) {
  BK48X9_Init(16,130);
  for (unsigned cycle=0;cycle<3;++cycle) {
    for (unsigned type=0;type<3;++type) {
      BK48X9_ConfigureVoiceTx(43850000u,16,1300,24000000u);
      if (!type) BK48X9_SetCTCSSFrequency(1000);
      else BK48X9_SetCDCSSCodeWord(DCS_GetGolayCodeWord(
        type==1 ? CODE_TYPE_DIGITAL : CODE_TYPE_REVERSE_DIGITAL,0));
      BK48X9_PrepareTransmit();
      assert(regs[0x52]==0x028F); // Fresh TX clears the prior tail mode.
      uint16_t frequency=regs[0x07], tone=regs[0x51], word=regs[0x08];
      BK48X9_SetupPowerAmplifier(90,43850000u);
      BK48X9_ToggleGpioOut(BK48X9_GPIO1_PIN29_PA_ENABLE,true);
      uint16_t pa=regs[0x36], dsp=regs[0x30];
      tailWrites=0;inTail=true;
      BK48X9_StartTxTail(type!=0);
      inTail=false;
      assert(regs[0x36]==pa && regs[0x30]==dsp && (regs[0x33] & 0x20));
      assert(regs[0x50]==(TEST_BK4829 ? 0xBB18 : 0xBB20));
      assert(regs[0x52]==(type ? 0x828F : 0xC28F));
      assert(regs[0x51]==(type ? (TEST_BK4829 ? 0x8040 : 0x804A) : tone));
      assert(regs[0x07]==frequency && regs[0x08]==word && tailWrites==(type ? 3u : 2u));
      BK48X9_ToggleGpioOut(BK48X9_GPIO1_PIN29_PA_ENABLE,false);
      BK48X9_WriteRegister(BK48X9_REG_36,0);
      BK48X9_Idle();BK48X9_ExitSubAu();
      assert(!regs[0x30] && !regs[0x51]);
    }
  }
  return 0;
}
