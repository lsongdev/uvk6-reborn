#include "drivers/bk48x9/bk48x9.h"
#include "drivers/bk48x9/internal.h"
#include "core/misc.h"

void BK48X9_ConfigureVoiceTx(uint32_t frequency, uint8_t mic, uint16_t deviation,
                             uint32_t vhfUhfBound) {
  BK48X9_EnterTxMute();
  BK48X9_Idle();
  BK48X9_SetRxMode(false);
  BK48X9_SetAF(BK48X9_AF_MUTE);
  BK48X9_DisableDTMF();
  BK48X9_WriteRegister(BK48X9_REG_3F, 0);
  BK48X9_ConfigureTxAudio(mic,deviation);
  BK48X9_SelectFilter(frequency, vhfUhfBound);
  BK48X9_SetFrequency(frequency);
}
