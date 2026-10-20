#include "drivers/bk48x9/bk48x9.h"
#include "drivers/bk48x9/internal.h"
#include "core/misc.h"
#include "hal/systick.h"

// DS-BK4829-E01 V1.0 guarantees 18--580 / 760--1160 MHz. Preserve
// the existing extended 660/1300 MHz tuning limits; add the documented
// 760--840 MHz span without imposing a narrower TX-only whitelist.
const RadioSpan BK48X9_FREQUENCY_RANGES[RADIO_RF_RANGES] = {
  {18000000u,660000001u}, {760000000u,1300000001u}
};

// Statically selected chip backend; register values preserved by trace regressions.
const BK48X9Chip BK48X9_CHIP={
  // Recommended squelch timing: open 5, close 3, as the stock firmware and
  // the reference set it. 1/1 also receives cleanly on this radio.
  .capabilities={4829,true,(1u<<0)|(1u<<4)|(1u<<9),5,3},
  .squelch_interrupts=0x000C,.vco_restart=0,.bus_delay_us=1,
  .lna_attenuation={28,24,19,0},
};

void BK48X9_SetAGC(uint8_t gainIndex) {
  if (gainIndex >= ARRAY_SIZE(GAIN_TABLE)) return;

    // Stock BK4829 profile (reference 5fedc09). Do not overwrite its DC
    // correction bits with the BK4819 defaults on every tune.
    static const uint16_t gains[] = {0x0318, 0x033A, 0x03DB, 0x03DF, 0x0210};
    for (unsigned i = 0; i < ARRAY_SIZE(gains); ++i)
      BK48X9_WriteRegister(BK48X9_REG_10 + i, gains[i]);
    if (gainIndex) BK48X9_WriteRegister(BK48X9_REG_13, GAIN_TABLE[gainIndex].regValue);
    BK48X9_WriteRegister(BK48X9_REG_49, 0x2AB2);
    BK48X9_WriteRegister(BK48X9_REG_7B, 0x73DC);
    uint16_t reg = BK48X9_ReadRegister(BK48X9_REG_7E);
    BK48X9_WriteRegister(BK48X9_REG_7E, (reg & ~0xF000u) | 0x3000u | (gainIndex ? 0x8000u : 0));
    return;

}

// Demodulator last configured; AM narrows with its own filter preset.
static bool amDemodulator;

void BK48X9_SetFilterBandwidth(BK48X9_FilterBandwidth_t bw) {
  // Project nominal width labels select the reference presets. These labels
  // are not measurements of the analog filter's -3 dB width. AM uses the
  // stock AM preset below 26k and the wide preset at 26k (reference 5fedc09).
  uint16_t value;
  switch (bw) {
  case BK48X9_FILTER_BW_6k: value = amDemodulator ? 0x345C : 0x2058; break;
  case BK48X9_FILTER_BW_12k: value = amDemodulator ? 0x345C : 0x4048; break;
  case BK48X9_FILTER_BW_26k: value = 0x3028; break;
  default: return;
  }
  BK48X9_WriteRegister(BK48X9_REG_43, value);
}

void BK48X9_SetAF(BK48X9_AF_Type_t af) { BK48X9_WriteRegister(BK48X9_REG_47,0x6042u | (af << 8)); }

void BK48X9_SetupSquelch(SQL sq, uint8_t delayOpen, uint8_t delayClose) {
  BK48X9_WriteRegister(BK48X9_REG_70,0);
  sq.no = Clamp(sq.no, 0, 127);
  sq.nc = Clamp(sq.nc, 0, 127);

  BK48X9_WriteRegister(BK48X9_REG_4D, 0xA000 | sq.gc);
  // Bit 8 as the stock BK4829 firmware sets it.
  BK48X9_WriteRegister(BK48X9_REG_4E, (1u << 14) | ((delayOpen & 7u) << 11) |
                                          ((delayClose & 3u) << 9) | (1u << 8) | sq.go);
  BK48X9_WriteRegister(BK48X9_REG_4F, (sq.nc << 8) | sq.no);
  BK48X9_WriteRegister(BK48X9_REG_78, (sq.ro << 8) | sq.rc);
}

bool BK48X9_IsSquelchOpen(void) { return BK48X9_SquelchEventOpen(); }

void BK48X9_SetCDCSSCodeWord(uint32_t codeWord) {
  BK48X9_WriteRegister(
      BK48X9_REG_51,
      0xA033u);

  BK48X9_WriteRegister(BK48X9_REG_07,
                       BK48X9_REG_07_MODE_CTC1 |
                           (2775U << BK48X9_REG_07_SHIFT_FREQUENCY));

  BK48X9_WriteRegister(BK48X9_REG_08, (codeWord >> 0) & 0xFFF);
  BK48X9_WriteRegister(BK48X9_REG_08, 0x8000 | ((codeWord >> 12) & 0xFFF));
}

void BK48X9_SetCTCSSFrequency(uint32_t freqControlWord) {
  uint16_t config = (freqControlWord == 2625) ? 0x944A : 0x904A;

  config &= ~0x0Fu;
  BK48X9_WriteRegister(BK48X9_REG_51, config);
  BK48X9_WriteRegister(BK48X9_REG_07, BK48X9_REG_07_MODE_CTC1 |
                                          (((freqControlWord * 206488u + 50000u) / 100000u)
                                           << BK48X9_REG_07_SHIFT_FREQUENCY));
}

void BK48X9_StartTxTail(bool digital) {
  BK48X9_EnterTxMute();
  // REG52 bit15 enables tail generation: DCS 134.4 Hz / CTCSS 180 degrees.
  BK48X9_WriteRegister(BK48X9_REG_52, digital ? 0x828F : 0xC28F);
  if (digital) BK48X9_WriteRegister(BK48X9_REG_51, 0x8040);
}

void BK48X9_EnterTxMute(void) { BK48X9_WriteRegister(BK48X9_REG_50,0xBB18); }

void BK48X9_ExitTxMute(void) { BK48X9_WriteRegister(BK48X9_REG_50,0x3B18); }

void BK48X9_RX_TurnOn(void) {
  BK48X9_WriteRegister(BK48X9_REG_36, 0x0000);

    BK48X9_WriteRegister(BK48X9_REG_37, 0x9F1F);
    BK48X9_WriteRegister(BK48X9_REG_30, 0);
    BK48X9_WriteRegister(BK48X9_REG_30, 0xBFF1);
    return;

}

void BK48X9_TxOn_Beep(void) {
  BK48X9_WriteRegister(BK48X9_REG_36,0);
  BK48X9_WriteRegister(BK48X9_REG_37, 0x9D1F);
  BK48X9_WriteRegister(BK48X9_REG_52, 0x028F);
  BK48X9_Idle();
  BK48X9_WriteRegister(BK48X9_REG_30, 0xC1FE);
}

void BK48X9_ExitBypass(void) {
  BK48X9_SetAF(BK48X9_AF_MUTE);
  // BK4829 retains its other AGC fields; only select normal voice filters.
  uint16_t value = BK48X9_ReadRegister(BK48X9_REG_7E);
  BK48X9_WriteRegister(BK48X9_REG_7E, (value & ~0x38u) | 0x28u);
}

void BK48X9_SetAFC(uint8_t level) { (void)level; }

void BK48X9_SetAFCSpeed(uint8_t speed) { (void)speed; }

__attribute__((noinline)) void BK48X9_ConfigureMicrophone(uint8_t mic) {
  BK48X9_WriteRegister(BK48X9_REG_19, 0x1041);
  BK48X9_WriteRegister(BK48X9_REG_7D, 0xE940 | (mic & 31u));
  // The K1 reference programs 19/7D but keeps the BK4829's REG_74 default.

}

void BK48X9_ConfigureTxAudio(uint8_t mic,uint16_t deviation) {
  BK48X9_SetFilterBandwidth(BK48X9_FILTER_BW_12k);
  BK48X9_ConfigureMicrophone(mic);
  (void)deviation;
  BK48X9_WriteRegister(BK48X9_REG_40,0x3516);
}

bool BK48X9_ConfigureDemodulator(bool am) {
  // Reference RADIO_SetModulation on the K1 (5fedc09), whose AM/FM register
  // values it copies from the stock UV-K1 firmware: demodulator, audio
  // profile (0x54/0x55), 0x3D, AF DAC gain 15, AFC on for FM only. AM keeps
  // 0x2B/0x2F as FM left them, as the stock firmware does.
  uint16_t reg31 = BK48X9_ReadRegister(BK48X9_REG_31);
  BK48X9_WriteRegister(BK48X9_REG_31, am ? reg31 | 1u : reg31 & ~1u);
  BK48X9_WriteRegister(0x42, am ? 0x6F5C : 0x6B5A);
  BK48X9_WriteRegister(0x2A, am ? 0x7434 : 0x7400);
  if (!am) {
    BK48X9_WriteRegister(BK48X9_REG_2B, 0x0000);
    BK48X9_WriteRegister(0x2F, 0x9890);
  }
  BK48X9_WriteRegister(0x54, 0x9009);
  BK48X9_WriteRegister(0x55, 0x31A9);
  BK48X9_WriteRegister(BK48X9_REG_48, (BK48X9_ReadRegister(BK48X9_REG_48) & ~0xFu) | 0xFu);
  BK48X9_WriteRegister(0x3D, 0x2AAB);
  uint16_t reg73 = BK48X9_ReadRegister(0x73);
  BK48X9_WriteRegister(0x73, am ? reg73 | (1u << 4) : reg73 & ~(1u << 4));
  amDemodulator = am;
  return true;
}

void BK48X9_MuteAudio(void) {
  BK48X9_SetAF(BK48X9_AF_MUTE);
}

void BK48X9_Init(uint8_t mic,uint16_t deviation) {
  if (!BK48X9_BeginInit()) return;

    // Reference: armel/uv-k1-k5v3-firmware-custom, 5fedc09, bk4829.c.
    // Keep the PA bias/gate disabled, including during initialization.
    static const struct { uint8_t reg; uint16_t value; } init[] = {
      {0x37,0x9D1F}, {0x36,0}, {0x7D,0xE920}, {0x40,0x3516},
      {0x1C,0x07C0}, {0x1D,0xE555}, {0x1E,0x4C58}, {0x1F,0xC65A},
      {0x3E,0x94C6}, {0x73,0x4691}, {0x77,0x88EF}, {0x19,0x1041},
      {0x28,0x0B40}, {0x29,0xAA00}, {0x2A,0x6600}, {0x2C,0x1822},
      {0x2F,0x9890}, {0x53,0x2028}, {0x7E,0x303E}, {0x46,0x600A},
      {0x4A,0x5430}, {0x07,0x61CE},
    };
    for (unsigned i = 0; i < ARRAY_SIZE(init); ++i)
      BK48X9_WriteRegister(init[i].reg, init[i].value);
    BK48X9_ResetGpioOutputs();
    BK48X9_SetAGC(AUTO_GAIN_INDEX);
    BK48X9_WriteRegister(BK48X9_REG_3F, 0);

  BK48X9_InitDtmfCoefficients();
  BK48X9_WriteRegister(BK48X9_REG_48,0x33A8);
  if (!BK48X9_WaitForCrystal()) return;
  BK48X9_ToggleGpioOut(BK48X9_GPIO0_PIN28_RX_ENABLE,true);
  BK48X9_DisableDTMF();
  (void)mic;(void)deviation;
  BK48X9_EndInit();
}

// Reference K1 spectrum (5fedc09 GetRssi): let the glitch count fall below
// 200 (at most ~50 us), then discard the first RSSI read after a retune,
// which may still reflect the previous point.
uint16_t BK48X9_GetScanRssi(void) {
  for (uint8_t guard = 50; guard-- && BK48X9_GetGlitch() >= 200; ) TIMER_DelayUs(1);
  (void)BK48X9_GetRSSI();
  return BK48X9_GetRSSI();
}
