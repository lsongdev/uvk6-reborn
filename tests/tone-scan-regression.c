// Real CSS scanner registers and both chip backends; GPIO/storage never touched.
#include "core/tone.h"
#include "drivers/bk48x9/bk48x9.h"
#include <assert.h>
static uint16_t regs[128];
static bool torn;
static unsigned highReads;
void BK48X9_BusInit(void) {}
void SYS_DelayMs(uint32_t ms) { (void)ms; }
uint16_t BK48X9_ReadRegister(BK48X9_REGISTER_t reg) {
  if (reg==0x0C) return 0;
  if (reg==0x69 && torn && ++highReads==2) return regs[reg]^1u;
  return regs[reg];
}
void BK48X9_WriteRegister(BK48X9_REGISTER_t reg, uint16_t value) {
  if (reg==0x33) assert(!(value & 0x20)); // A scan must never turn PA on.
  regs[reg]=value;
}
static uint32_t rotate(uint32_t w) { return (w >> 1) | ((w & 1u) << 22); }
// Original matcher retained as an independent exhaustive equivalence oracle.
static RadioTone originalCtcss(uint16_t hz10) {
  unsigned best=UINT16_MAX;RadioTone result=0;
  for (unsigned i=0;i<50;++i) {
    unsigned standard=CTCSS_Options[i];
    unsigned delta=hz10>standard ? hz10-standard : standard-hz10;
    if (delta<best && (delta<=10u || delta*200u<=standard)) {
      best=delta;result=i+1u;
    }
  }
  return result;
}
int main(void) {
  for (unsigned hz10=0;hz10<=UINT16_MAX;++hz10)
    assert(TONE_DecodeCtcss(hz10)==originalCtcss(hz10));
  for (unsigned i=0;i<50;++i) {
    assert(TONE_DecodeCtcss(CTCSS_Options[i])==i+1);
    assert(TONE_DecodeCtcss(CTCSS_Options[i]+1u)==i+1);
    assert(TONE_DecodeCtcss(CTCSS_Options[i]-1u)==i+1);
  }
  assert(TONE_DecodeCtcss(0)==0 && TONE_DecodeCtcss(550)==0);
  assert(TONE_DecodeCtcss(3000)==0 && TONE_DecodeCtcss(680)==1);
  assert(!TONE_DecodeCtcss(682)); // Gap between 67.0 and 69.3: no forced match.
  assert(TONE_DecodeDcs(0x763813)==51); // D023N reference vector.
  assert(TONE_DecodeDcs(0x763813 ^ 0x7FFFFF)==58); // D023I == cyclic D047N.
  assert(!TONE_DecodeDcs(0) && !TONE_DecodeDcs(0x7FFFFF));
  for (unsigned i=0;i<104;++i) {
    for (unsigned inverse=0;inverse<2;++inverse) {
      uint32_t frame=DCS_GetGolayCodeWord(inverse ? CODE_TYPE_REVERSE_DIGITAL : CODE_TYPE_DIGITAL,i);
      uint32_t word=frame;
      for (unsigned bit=0;bit<23;++bit) {
        RadioTone got=TONE_DecodeDcs(word);
        assert(got>=51 && got<=154);
        if (!inverse) assert(got==51+i);
        // The displayed normal code must represent the same cyclic waveform.
        uint32_t expected=DCS_GetGolayCodeWord(CODE_TYPE_DIGITAL,TONE_Code(got));
        unsigned shift=0;
        while (expected!=word && shift++<23) expected=rotate(expected);
        assert(expected==word);
        assert(!TONE_DecodeDcs(word ^ 1u)); // A single damaged bit is not accepted.
        word=rotate(word);
      }
    }
  }
  BK48X9_Init(16,130);
  BK48X9_SetFrequency(43850000u);
  BK48X9_StartToneScan();
  assert(regs[0x51]==0x0300 && regs[0x30] && !(regs[0x33]&0x20));
  assert(regs[0x38]==(43850000u & 0xFFFFu) && regs[0x39]==(43850000u >> 16));
  uint32_t value=0xDEADBEEF;
  regs[0x69]=regs[0x68]=0x8000;
  assert(BK48X9_ReadToneScan(&value)==BK48X9_CSS_RESULT_NOT_FOUND && value==0xDEADBEEF);
  regs[0x68]=1828;
  assert(BK48X9_ReadToneScan(&value)==BK48X9_CSS_RESULT_CTCSS && value==885);
  regs[0x69]=0x763;regs[0x6A]=0x813;
  assert(BK48X9_ReadToneScan(&value)==BK48X9_CSS_RESULT_CDCSS && value==0x763813);
  torn=true;
  assert(BK48X9_ReadToneScan(&value)==BK48X9_CSS_RESULT_NOT_FOUND);
  return 0;
}
