#include "drivers/bk48x9/bk48x9.h"
#include "drivers/bk48x9/internal.h"
#include "core/misc.h"

// Statically selected chip backend; register values preserved by trace regressions.
const BK48X9Chip BK48X9_CHIP={
  .capabilities={4819,true,(1u<<10)-1u,1,1}, // The long-standing K6 delays.
  .squelch_interrupts=0,.vco_restart=0x0200,.bus_delay_us=0,
  .lna_attenuation={19,16,11,0},
};

static void configure_agc_registers(void) {
  BK48X9_WriteRegister(0x12, (3u << 8) | (3u << 5) | (3u << 3) | (4u << 0));
  BK48X9_WriteRegister(0x11, (2u << 8) | (3u << 5) | (3u << 3) | (3u << 0));
  BK48X9_WriteRegister(0x10, (0u << 8) | (3u << 5) | (3u << 3) | (2u << 0));
  BK48X9_WriteRegister(0x14, (0u << 8) | (0u << 5) | (3u << 3) | (1u << 0));
}

void BK48X9_SetAGC(uint8_t gainIndex) {
  if (gainIndex >= ARRAY_SIZE(GAIN_TABLE)) return;

  const bool enableAgc = (gainIndex == AUTO_GAIN_INDEX);
  uint16_t regVal = BK48X9_ReadRegister(BK48X9_REG_7E);

  BK48X9_WriteRegister(BK48X9_REG_7E,
                       (regVal & ~(1 << 15) & ~(0b111 << 12)) |
                           (!enableAgc << 15) | // AGC fix mode
                           (3u << 12) |         // AGC fix index
                           (5u << 3) |          // Default DC
                           (6u << 0));

  if (enableAgc) {
    BK48X9_WriteRegister(BK48X9_REG_13, 0x03BE);
  } else {
    BK48X9_WriteRegister(BK48X9_REG_13, GAIN_TABLE[gainIndex].regValue);
  }

  configure_agc_registers();

  BK48X9_WriteRegister(BK48X9_REG_49, (84u << 7) | 56u);
  BK48X9_WriteRegister(BK48X9_REG_7B, 0x8420); // 0x8420
}

void BK48X9_SetFilterBandwidth(BK48X9_FilterBandwidth_t bw) {

  if (bw > 9)
    return;

  bw = 9 - bw; // Fix for incremental order

  static const uint8_t rf[] = {7, 5, 4, 3, 2, 1, 3, 1, 1, 0};
  static const uint8_t af[] = {4, 5, 6, 7, 0, 0, 3, 0, 2, 1};
  static const uint8_t bs[] = {2, 2, 2, 2, 2, 2, 0, 0, 1, 1};

  const uint16_t value = //
      (0u << 15)         //
      | (rf[bw] << 12)   //
      | (rf[bw] << 9)    //
      | (af[bw] << 6)    //
      | (bs[bw] << 4)    //
      | (1u << 3)        //
      | (0u << 2)        //
      | (0u << 0);       //

  // BK48X9_WriteRegister(0x43, 0x347C); // AM 0b11010001111100
  // BK48X9_WriteRegister(0x43, 0x3028); // FM 0b11000000101000
  BK48X9_WriteRegister(BK48X9_REG_43, value);
}

void BK48X9_SetAF(BK48X9_AF_Type_t af) { BK48X9_WriteRegister(BK48X9_REG_47,0x6040u | (af << 8)); }

void BK48X9_SetupSquelch(SQL sq, uint8_t delayOpen, uint8_t delayClose) {
  sq.no = Clamp(sq.no, 0, 127);
  sq.nc = Clamp(sq.nc, 0, 127);

  BK48X9_WriteRegister(BK48X9_REG_4D, 0xA000 | sq.gc);
  BK48X9_WriteRegister(BK48X9_REG_4E, (1u << 14) | (delayOpen << 11) |
                                          (delayClose << 9) | (0u) | sq.go);
  BK48X9_WriteRegister(BK48X9_REG_4F, (sq.nc << 8) | sq.no);
  BK48X9_WriteRegister(BK48X9_REG_78, (sq.ro << 8) | sq.rc);
}

bool BK48X9_IsSquelchOpen(void) { return (BK48X9_ReadRegister(BK48X9_REG_0C) >> 1) & 1u; }

void BK48X9_SetCDCSSCodeWord(uint32_t codeWord) {
  BK48X9_WriteRegister(
      BK48X9_REG_51,
      BK48X9_REG_51_ENABLE_CxCSS | BK48X9_REG_51_GPIO6_PIN2_NORMAL | BK48X9_REG_51_TX_CDCSS_POSITIVE | BK48X9_REG_51_MODE_CDCSS | BK48X9_REG_51_CDCSS_23_BIT | BK48X9_REG_51_1050HZ_NO_DETECTION | BK48X9_REG_51_AUTO_CDCSS_BW_ENABLE | BK48X9_REG_51_AUTO_CTCSS_BW_ENABLE | (51u << BK48X9_REG_51_SHIFT_CxCSS_TX_GAIN1));

  BK48X9_WriteRegister(BK48X9_REG_07,
                       BK48X9_REG_07_MODE_CTC1 |
                           (2775U << BK48X9_REG_07_SHIFT_FREQUENCY));

  BK48X9_WriteRegister(BK48X9_REG_08, (codeWord >> 0) & 0xFFF);
  BK48X9_WriteRegister(BK48X9_REG_08, 0x8000 | ((codeWord >> 12) & 0xFFF));
}

void BK48X9_SetCTCSSFrequency(uint32_t freqControlWord) {
  uint16_t config = (freqControlWord == 2625) ? 0x944A : 0x904A;


  BK48X9_WriteRegister(BK48X9_REG_51, config);
  BK48X9_WriteRegister(BK48X9_REG_07, BK48X9_REG_07_MODE_CTC1 |
                                          ((freqControlWord * 2065u / 1000u)
                                           << BK48X9_REG_07_SHIFT_FREQUENCY));
}

void BK48X9_EnterTxMute(void) { BK48X9_WriteRegister(BK48X9_REG_50,0xBB20); }

void BK48X9_ExitTxMute(void) { BK48X9_WriteRegister(BK48X9_REG_50,0x3B20); }

void BK48X9_RX_TurnOn(void) {
  BK48X9_WriteRegister(BK48X9_REG_36, 0x0000);

  BK48X9_WriteRegister(BK48X9_REG_37, 0x1F0F);
  BK48X9_WriteRegister(BK48X9_REG_30, 0x0200);

  BK48X9_WriteRegister(
      BK48X9_REG_30,
      BK48X9_REG_30_ENABLE_VCO_CALIB | BK48X9_REG_30_DISABLE_UNKNOWN |
          BK48X9_REG_30_ENABLE_RX_LINK | BK48X9_REG_30_ENABLE_AF_DAC |
          BK48X9_REG_30_ENABLE_DISC_MODE | BK48X9_REG_30_ENABLE_PLL_VCO |
          BK48X9_REG_30_DISABLE_PA_GAIN | BK48X9_REG_30_DISABLE_MIC_ADC |
          BK48X9_REG_30_DISABLE_TX_DSP | BK48X9_REG_30_ENABLE_RX_DSP);
}

void BK48X9_TxOn_Beep(void) {

  BK48X9_WriteRegister(BK48X9_REG_37, 0x1D0F);
  BK48X9_WriteRegister(BK48X9_REG_52, 0x028F);
  BK48X9_Idle();
  BK48X9_WriteRegister(BK48X9_REG_30, 0xC1FE);
}

void BK48X9_ExitBypass(void) {
  BK48X9_SetAF(BK48X9_AF_MUTE);
  // BK4829 retains its other AGC fields; only select normal voice filters.
  uint16_t value = 0x302E;
  BK48X9_WriteRegister(BK48X9_REG_7E, (value & ~0x38u) | 0x28u);
}

void BK48X9_XtalSet(XtalMode mode) {
  uint16_t ifset = 0x2AAB;
  uint16_t xtal = 20360;

  switch (mode) {
  case XTAL_0_13M:
    xtal = 20232;
    ifset = 0x3555;
    break;
  case XTAL_1_19_2M:
    xtal = 20296;
    ifset = 0x2E39;
    break;
  case XTAL_2_26M:
    // Use defaults
    break;
  case XTAL_3_38_4M:
    xtal = 20424;
    ifset = 0x271C;
    break;
  }

  BK48X9_WriteRegister(0x3C, xtal);
  BK48X9_WriteRegister(0x3D, ifset);
}

void BK48X9_SetAFC(uint8_t level) {
  if (level > 8) {
    level = 8;
  }

  uint16_t reg_val = BK48X9_ReadRegister(0x73);

  reg_val &= ~((0xFu << 11) | (1u << 4));

  uint8_t afc_level;
  if (level == 0) {
    // Disable AFC with default level
    afc_level = 7u;
    reg_val |= (1u << 4);
  } else {
    // Enable AFC (disable bit remains 0) with calculated level
    afc_level = 8 - level;
  }

  reg_val |= (afc_level << 11);

  BK48X9_WriteRegister(0x73, reg_val);
}

void BK48X9_SetAFCSpeed(uint8_t speed) {
  if (speed > 63) {
    speed = 63;
  }

  uint16_t reg_val = BK48X9_ReadRegister(0x73);

  reg_val &= ~(63 << 5);
  reg_val |= ((63 - speed) << 5);

  BK48X9_WriteRegister(0x73, reg_val);
}

__attribute__((noinline)) void BK48X9_ConfigureMicrophone(uint8_t mic) {
  BK48X9_WriteRegister(BK48X9_REG_19, 0x1041);
  BK48X9_WriteRegister(BK48X9_REG_7D, 0xE940 | (mic & 31u));
  BK48X9_WriteRegister(0x74, 0xAF1F);
}

void BK48X9_ConfigureTxAudio(uint8_t mic,uint16_t deviation) {
  BK48X9_SetFilterBandwidth(BK48X9_FILTER_BW_9k);
  BK48X9_ConfigureMicrophone(mic);
  BK48X9_WriteRegister(BK48X9_REG_40,(BK48X9_ReadRegister(BK48X9_REG_40)&~0x7FFu) | (deviation&0x7FFu) | (1u<<12));
}

bool BK48X9_ConfigureDemodulator(bool am) {

  BK48X9_SetRegValue(RS_AFC_DIS, 0);
  BK48X9_XtalSet(XTAL_2_26M);
  uint16_t reg31 = BK48X9_ReadRegister(BK48X9_REG_31);
  BK48X9_WriteRegister(BK48X9_REG_31, am ? reg31 | 1u : reg31 & ~1u);
  BK48X9_WriteRegister(0x42, am ? 0x6F5C : 0x6B5A);
  BK48X9_WriteRegister(0x2A, am ? 0x7434 : 0x7400);
  BK48X9_WriteRegister(BK48X9_REG_2B, am ? 0x0600 : 0);
  BK48X9_WriteRegister(0x2F, am ? 0x9990 : 0x9890);

  return true;
}

void BK48X9_MuteAudio(void) {
  BK48X9_SetAF(BK48X9_AF_MUTE);
  BK48X9_ToggleAFDAC(false);
}

void BK48X9_Init(uint8_t mic,uint16_t deviation) {
  if (!BK48X9_BeginInit()) return;

  // power up rf
  BK48X9_WriteRegister(BK48X9_REG_37, 0x1D0F);

  BK48X9_ResetGpioOutputs();

  // PA
  BK48X9_SetupPowerAmplifier(0, 0);

  BK48X9_WriteRegister(BK48X9_REG_1E, 0x4c58); // for some revisions
  BK48X9_WriteRegister(BK48X9_REG_1F, 0x5454); // VCO, PLL

  BK48X9_SetAGC(AUTO_GAIN_INDEX);

  BK48X9_WriteRegister(BK48X9_REG_3F, 0); // interrupts

  BK48X9_WriteRegister(BK48X9_REG_3E, 0xA037); // band sel tres

  BK48X9_InitDtmfCoefficients();
  BK48X9_WriteRegister(BK48X9_REG_48,0xB3A8);
  if (!BK48X9_WaitForCrystal()) return;
  BK48X9_ConfigureMicrophone(mic);
  BK48X9_ToggleGpioOut(BK48X9_GPIO0_PIN28_RX_ENABLE,true);
  BK48X9_DisableDTMF();
  BK48X9_WriteRegister(0x40,(BK48X9_ReadRegister(0x40)&~0x7FFu) | (deviation*10u) | (1u<<12));
  BK48X9_EndInit();
}
