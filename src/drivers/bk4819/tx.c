#include "drivers/bk4819/bk4819.h"

// Shared by startup and each voice-TX preparation; do not duplicate writes.
__attribute__((noinline)) void BK4819_ConfigureMicrophone(uint8_t mic) {
  BK4819_WriteRegister(BK4819_REG_19, 0x1041);
  BK4819_WriteRegister(BK4819_REG_7D, 0xE940 | (mic & 31u));
  BK4819_WriteRegister(0x74, 0xAF1F);
}

void BK4819_ConfigureVoiceTx(uint32_t frequency, uint8_t mic, uint16_t deviation,
                             uint32_t vhfUhfBound) {
  BK4819_EnterTxMute();
  BK4819_Idle();
  BK4819_SetRxMode(false);
  BK4819_SetAF(BK4819_AF_MUTE);
  BK4819_DisableDTMF();
  BK4819_WriteRegister(BK4819_REG_3F, 0);
  BK4819_SetFilterBandwidth(BK4819_FILTER_BW_9k);
  BK4819_ConfigureMicrophone(mic);
  BK4819_WriteRegister(BK4819_REG_40,
    (BK4819_ReadRegister(BK4819_REG_40) & ~0x7FFu) | (deviation & 0x7FFu) | (1u << 12));
  BK4819_SelectFilter(frequency, vhfUhfBound);
  BK4819_SetFrequency(frequency);
}
