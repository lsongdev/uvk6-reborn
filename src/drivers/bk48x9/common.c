#include "drivers/bk48x9/bk48x9.h"
#include "drivers/bk48x9/internal.h"
#include "core/misc.h"
#include "hal/system.h"

const Gain GAIN_TABLE[32] = {
    {0x3ff, 0},  // AUTO
    {0x3ff, 0},  //
    {0x3f7, 3},  //
    {0x3ef, 6},  //
    {0x3e7, 8},  //
    {0x3e6, 11}, //
    {0x3e5, 14}, //
    {0x3e4, 17}, //
    {0x3d3, 20}, //
    {0x3b3, 22}, //
    {0x3c3, 25}, //
    {0x3b2, 28}, //
    {0x3c2, 31}, //
    {0x3b1, 34}, //
    {0x3f0, 36}, //
    {0x3e8, 39}, //
    {0x390, 42}, //
    {0x3a0, 45}, //
    {0x368, 48}, //
    {0x360, 50}, //
    {0x348, 53}, //
    {0x2a0, 56}, //
    {0x301, 59}, //
    {0x20a, 62}, //
    {0x248, 64}, //
    {0x10a, 67}, //
    {0x201, 70}, //
    {0x109, 73}, //
    {0x200, 76}, //
    {0x1, 78},   //
    {0x100, 81}, //
    {0x0, 84},   //

};

static uint16_t gGpioOutState=0x9000;
static uint8_t gSelectedFilter=255;
static bool initialized;

void BK48X9_SetRegValue(RegisterSpec spec, uint16_t value) {
  uint16_t reg = BK48X9_ReadRegister(spec.num);
  reg &= ~(spec.mask << spec.offset);
  BK48X9_WriteRegister(spec.num, reg | (value << spec.offset));
}

void BK48X9_Idle(void) { BK48X9_WriteRegister(BK48X9_REG_30, 0x0000); }

void BK48X9_ToggleGpioOut(BK48X9_GPIO_PIN_t pin, bool enable) {
  const uint16_t pin_bit = 0x40U >> pin;

  if (enable) {
    gGpioOutState |= pin_bit;
  } else {
    gGpioOutState &= ~pin_bit;
  }

  BK48X9_WriteRegister(BK48X9_REG_33, gGpioOutState);
}

int8_t BK48X9_GetAgcIndex() {
  int8_t idx = (BK48X9_ReadRegister(BK48X9_REG_7E) >> 12) & 7;
  if (idx > 3) {
    idx -= 8;
  }
  return idx;
}

uint8_t BK48X9_GetAttenuation() {
  BK48X9_REGISTER_t reg;
  switch (BK48X9_GetAgcIndex()) {
  case 0:
    reg = BK48X9_REG_10;
    break;
  case 1:
    reg = BK48X9_REG_11;
    break;
  case 2:
    reg = BK48X9_REG_12;
    break;
  case 3:
    reg = BK48X9_REG_13;
    break;
  default: // Indexes -1..-4 share the lowest gain-table register.
    reg = BK48X9_REG_14;
    break;
  }
  uint16_t v = BK48X9_ReadRegister(reg);
  return BK48X9_CHIP.lna_attenuation[(v >> 8) & 3] +
         (uint8_t[]){24, 19, 14, 9, 6, 4, 2, 0}[(v >> 5) & 7] +
         (uint8_t[]){8, 6, 3, 0}[(v >> 3) & 3] +
         (uint8_t[]){33, 27, 21, 15, 9, 6, 3, 0}[v & 7];
}

void BK48X9_SelectFilterEx(Filter filter) {
  if (gSelectedFilter == filter) {
    return;
  }
  gSelectedFilter = filter;
  const uint16_t PIN_BIT_VHF = 0x40U >> BK48X9_GPIO4_PIN32_VHF_LNA;
  const uint16_t PIN_BIT_UHF = 0x40U >> BK48X9_GPIO3_PIN31_UHF_LNA;

  if (filter == FILTER_VHF) {
    gGpioOutState |= PIN_BIT_VHF;
  } else {
    gGpioOutState &= ~PIN_BIT_VHF;
  }

  if (filter == FILTER_UHF) {
    gGpioOutState |= PIN_BIT_UHF;
  } else {
    gGpioOutState &= ~PIN_BIT_UHF;
  }

  BK48X9_WriteRegister(BK48X9_REG_33, gGpioOutState);
}

void BK48X9_SelectFilter(uint32_t frequency, uint32_t vhfUhfBound) {
  Filter filter = (frequency < vhfUhfBound) ? FILTER_VHF : FILTER_UHF;

  BK48X9_SelectFilterEx(filter);
}

void BK48X9_SetFrequency(uint32_t freq) {
  static uint16_t prev_low = 0;
  static uint16_t prev_high = 0;

  uint16_t low = freq & 0xFFFF;
  uint16_t high = (freq >> 16) & 0xFFFF;

  if (low != prev_low) {
    BK48X9_WriteRegister(BK48X9_REG_38, low);
    prev_low = low;
  }

  if (high != prev_high) {
    BK48X9_WriteRegister(BK48X9_REG_39, high);
    prev_high = high;
  }
}

void BK48X9_TuneTo(uint32_t freq, bool precise) {
  // A frequency-only step must not inherit the last channel's open state.
  // Re-arm before restarting RX, so fresh events are not drained afterwards.
  if (BK48X9_CHIP.squelch_interrupts) BK48X9_SquelchEventsReset();
  BK48X9_SetFrequency(freq);

  uint16_t reg = BK48X9_ReadRegister(BK48X9_REG_30);

  if (precise) {
    BK48X9_WriteRegister(BK48X9_REG_30, BK48X9_CHIP.vco_restart);
  } else {
    BK48X9_WriteRegister(BK48X9_REG_30, reg & ~BK48X9_REG_30_ENABLE_VCO_CALIB);
  }

  BK48X9_WriteRegister(BK48X9_REG_30, reg);
}

void BK48X9_Squelch(uint8_t sql, uint8_t openDelay, uint8_t closeDelay) {
  BK48X9_SetupSquelch(GetSql(sql), openDelay, closeDelay);
}

void BK48X9_ExitSubAu(void) { BK48X9_WriteRegister(BK48X9_REG_51, 0x0000); }

void BK48X9_DisableDTMF(void) { BK48X9_WriteRegister(BK48X9_REG_24, 0); }

void BK48X9_PrepareTransmit(void) {
  BK48X9_ExitBypass();
  BK48X9_ExitTxMute();
  BK48X9_TxOn_Beep();
}

void BK48X9_SetupPowerAmplifier(uint8_t bias, uint32_t frequency) {
  uint8_t gain = (frequency < VHF_UHF_BOUND2) ? 0x08 : 0x22;
  BK48X9_WriteRegister(BK48X9_REG_36, (bias << 8) | 0x80U | gain);
}

uint16_t BK48X9_GetRSSI(void) {
  return BK48X9_ReadRegister(BK48X9_REG_67) & 0x1FF;
}

uint8_t BK48X9_GetNoise(void) {
  return BK48X9_ReadRegister(BK48X9_REG_65) & 0x7F;
}

uint8_t BK48X9_GetGlitch(void) {
  return BK48X9_ReadRegister(BK48X9_REG_63) & 0xFF;
}

void BK48X9_SetRxCalibration(int16_t trim, uint8_t volume, uint8_t dac) {
  if (trim < -1000 || trim > 1000) trim = 0;
  if (volume > 63) volume = 58;
  if (dac > 15) dac = 8;
  // Both K1 references load REG_3B from factory offset 0x188 and restore
  // calibrated AF gain at runtime. REG_3C is deliberately left untouched.
  BK48X9_WriteRegister(BK48X9_REG_3B, (uint16_t)(22656 + trim));
  BK48X9_WriteRegister(BK48X9_REG_48, 0xB000u | ((uint16_t)volume << 4) | dac);
}

void BK48X9_ToggleAFDAC(bool enable) {
  uint16_t reg = BK48X9_ReadRegister(BK48X9_REG_30);
  reg &= ~BK48X9_REG_30_ENABLE_AF_DAC;
  if (enable) {
    reg |= BK48X9_REG_30_ENABLE_AF_DAC;
  }
  BK48X9_WriteRegister(BK48X9_REG_30, reg);
}

static const uint16_t bandwidthHz[]={6000,7000,9000,10000,12000,14000,17000,20000,23000,26000};
bool BK48X9_BandwidthFromHz(uint32_t hz, uint16_t *index) {
  for (uint8_t i = 0; i < ARRAY_SIZE(bandwidthHz); ++i) {
    if (bandwidthHz[i] == hz &&
        (BK48X9_CHIP.capabilities.bandwidth_mask & (1u << i))) {
      *index = i;
      return true;
    }
  }
  return false;
}

BK48X9Capabilities BK48X9_GetCapabilities(void) { return BK48X9_CHIP.capabilities; }

void BK48X9_ResetGpioOutputs(void) {
  gGpioOutState=0x9000;
  BK48X9_WriteRegister(BK48X9_REG_33,gGpioOutState);
}
bool BK48X9_BeginInit(void) {
  if (initialized) return false;
  gSelectedFilter=255;
  BK48X9_InvalidateRxMode();
  BK48X9_BusInit();
  BK48X9_WriteRegister(BK48X9_REG_00,0x8000);
  BK48X9_WriteRegister(BK48X9_REG_00,0);
  return true;
}
void BK48X9_EndInit(void) { initialized=true; }
void BK48X9_InitDtmfCoefficients(void) {
  static const uint8_t coefficients[]={111,107,103,98,80,71,58,44,65,55,37,23,228,203,181,159};
  for (uint8_t i=0;i<ARRAY_SIZE(coefficients);++i)
    BK48X9_WriteRegister(0x09,(i << 12) | coefficients[i]);
}
bool BK48X9_WaitForCrystal(void) {
  for (unsigned elapsed=0;elapsed<100;++elapsed) {
    if (!(BK48X9_ReadRegister(BK48X9_REG_0C) & 1u)) return true;
    BK48X9_WriteRegister(BK48X9_REG_02,0);
    SYS_DelayMs(1);
  }
  BK48X9_Idle();
  return false;
}
