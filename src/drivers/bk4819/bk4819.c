#include "core/device-config.h"
#include "drivers/bk4819/bk4819.h"
#include "hal/gpio.h"
#include "hal/system.h"
#include "hal/systick.h"
#include "hal/uart.h"
#include "external/printf/printf.h"
#include "drivers/dp32g030/gpio.h"
#include "drivers/dp32g030/portcon.h"
#include "core/misc.h"
#include "core/settings.h"
#include "hal/audio.h"
#include "drivers/bk4819/bk4819-regs.h"
#include <stdint.h>
#include <stdio.h>

#define SHORT_DELAY()                                                          \
  __asm volatile("nop\n nop\n nop\n nop\n nop\n"                               \
                 "nop\n nop\n nop\n nop\n nop\n"                               \
                 "nop\n nop\n nop\n"); // ~13 NOPs для ~0.27 мкс @48MHz

// ============================================================================
// Constants
// ============================================================================



static const uint8_t DTMF_COEFFS[] = {111, 107, 103, 98, 80,  71,  58,  44,
                                      65,  55,  37,  23, 228, 203, 181, 159};

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

// AGC configuration constants
typedef struct {
  uint8_t lo;
  uint8_t low;
  uint8_t high;
} AgcConfig;

static const AgcConfig AGC_DEFAULT = {0, 56, 84};

// ============================================================================
// State Variables
// ============================================================================

static uint16_t gGpioOutState = 0x9000;
static uint8_t gSelectedFilter = 255;
static uint32_t gLastFrequency = 0;

// ============================================================================
// Low-Level GPIO and SPI Operations
// ============================================================================

static inline void gpio_set_scn(bool high) {
  if (high) {
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
  } else {
    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
  }
}

static inline void gpio_set_scl(bool high) {
  if (high) {
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
  } else {
    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
  }
}

static inline void gpio_set_sda(bool high) {
  if (high) {
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
  } else {
    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
  }
}

static void spi_write_byte(uint8_t data) {
  gpio_set_scl(false);

  for (uint8_t i = 0; i < 8; i++) {
    gpio_set_sda(data & 0x80);
    SHORT_DELAY();
    gpio_set_scl(true);
    SHORT_DELAY();
    data <<= 1;
    gpio_set_scl(false);
    SHORT_DELAY();
  }
}

static uint16_t spi_read_word(void) {
  // Configure SDA as input
  PORTCON_PORTC_IE = (PORTCON_PORTC_IE & ~PORTCON_PORTC_IE_C2_MASK) |
                     PORTCON_PORTC_IE_C2_BITS_ENABLE;
  GPIOC->DIR = (GPIOC->DIR & ~GPIO_DIR_2_MASK) | GPIO_DIR_2_BITS_INPUT;
  TIMER_DelayUs(1);

  uint16_t value = 0;
  for (uint8_t i = 0; i < 16; i++) {
    value <<= 1;
    value |= GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
    gpio_set_scl(true);
    SHORT_DELAY();
    gpio_set_scl(false);
    SHORT_DELAY();
  }

  // Configure SDA back to output
  PORTCON_PORTC_IE = (PORTCON_PORTC_IE & ~PORTCON_PORTC_IE_C2_MASK) |
                     PORTCON_PORTC_IE_C2_BITS_DISABLE;
  GPIOC->DIR = (GPIOC->DIR & ~GPIO_DIR_2_MASK) | GPIO_DIR_2_BITS_OUTPUT;

  return value;
}

static void spi_write_word(uint16_t data) {
  gpio_set_scl(false);

  for (uint8_t i = 0; i < 16; i++) {
    gpio_set_sda(data & 0x8000);
    SHORT_DELAY();
    gpio_set_scl(true);
    data <<= 1;
    SHORT_DELAY();
    gpio_set_scl(false);
    SHORT_DELAY();
  }
}

// ============================================================================
// Register Access
// ============================================================================

static uint16_t reg30state = 0xffff;

uint16_t BK4819_ReadRegister(BK4819_REGISTER_t reg) {
  if (reg == BK4819_REG_30 && reg30state != 0xffff) {
    return reg30state;
  }
  gpio_set_scn(true);
  gpio_set_scl(false);
  __asm volatile("nop \n"
                 "nop \n");
  gpio_set_scn(false);

  spi_write_byte(reg | 0x80);
  uint16_t value = spi_read_word();

  gpio_set_scn(true);
  gpio_set_scl(true);
  gpio_set_sda(true);

  return value;
}

void BK4819_WriteRegister(BK4819_REGISTER_t reg, uint16_t data) {
  if (reg == BK4819_REG_30) {
    reg30state = data;
  }
  gpio_set_scn(true);
  gpio_set_scl(false);
  __asm volatile("nop \n"
                 "nop \n");
  gpio_set_scn(false);

  spi_write_byte(reg);
  spi_write_word(data);

  gpio_set_scn(true);
  gpio_set_scl(true);
}


void BK4819_SetRegValue(RegisterSpec spec, uint16_t value) {
  uint16_t reg = BK4819_ReadRegister(spec.num);
  reg &= ~(spec.mask << spec.offset);
  BK4819_WriteRegister(spec.num, reg | (value << spec.offset));
}

// ============================================================================
// Utility Functions
// ============================================================================


void BK4819_Idle(void) { BK4819_WriteRegister(BK4819_REG_30, 0x0000); }


// ============================================================================
// GPIO Control
// ============================================================================

void BK4819_ToggleGpioOut(BK4819_GPIO_PIN_t pin, bool enable) {
  const uint16_t pin_bit = 0x40U >> pin;

  if (enable) {
    gGpioOutState |= pin_bit;
  } else {
    gGpioOutState &= ~pin_bit;
  }

  BK4819_WriteRegister(BK4819_REG_33, gGpioOutState);
}

// ============================================================================
// AGC Configuration
// ============================================================================

static void configure_agc_registers(void) {
  BK4819_WriteRegister(0x12, (3u << 8) | (3u << 5) | (3u << 3) | (4u << 0));
  BK4819_WriteRegister(0x11, (2u << 8) | (3u << 5) | (3u << 3) | (3u << 0));
  BK4819_WriteRegister(0x10, (0u << 8) | (3u << 5) | (3u << 3) | (2u << 0));
  BK4819_WriteRegister(0x14, (0u << 8) | (0u << 5) | (3u << 3) | (1u << 0));
}

int8_t BK4819_GetAgcIndex() {
  int8_t idx = (BK4819_ReadRegister(BK4819_REG_7E) >> 12) & 7;
  if (idx > 3) {
    idx -= 8;
  }
  return idx;
}

uint8_t BK4819_GetAttenuation() {
  BK4819_REGISTER_t reg;
  switch (BK4819_GetAgcIndex()) {
  case 0:
    reg = BK4819_REG_10;
    break;
  case 1:
    reg = BK4819_REG_11;
    break;
  case 2:
    reg = BK4819_REG_12;
    break;
  case 3:
    reg = BK4819_REG_13;
    break;
  default: // Indexes -1..-4 share the lowest gain-table register.
    reg = BK4819_REG_14;
    break;
  }
  uint16_t v = BK4819_ReadRegister(reg);
  return (uint8_t[]){19, 16, 11, 0}[(v >> 8) & 3] +
         (uint8_t[]){24, 19, 14, 9, 6, 4, 2, 0}[(v >> 5) & 7] +
         (uint8_t[]){8, 6, 3, 0}[(v >> 3) & 3] +
         (uint8_t[]){33, 27, 21, 15, 9, 6, 3, 0}[v & 7];
}

void BK4819_SetAGC(uint8_t gainIndex) {
  const bool enableAgc = (gainIndex == AUTO_GAIN_INDEX);
  uint16_t regVal = BK4819_ReadRegister(BK4819_REG_7E);

  BK4819_WriteRegister(BK4819_REG_7E,
                       (regVal & ~(1 << 15) & ~(0b111 << 12)) |
                           (!enableAgc << 15) | // AGC fix mode
                           (3u << 12) |         // AGC fix index
                           (5u << 3) |          // Default DC
                           (6u << 0));

  if (enableAgc) {
    BK4819_WriteRegister(BK4819_REG_13, 0x03BE);
  } else {
    BK4819_WriteRegister(BK4819_REG_13, GAIN_TABLE[gainIndex].regValue);
  }

  configure_agc_registers();

  const AgcConfig *config = &AGC_DEFAULT;
  BK4819_WriteRegister(BK4819_REG_49, (config->lo << 14) | (config->high << 7) |
                                          (config->low << 0));
  BK4819_WriteRegister(BK4819_REG_7B, 0x8420); // 0x8420
}

// ============================================================================
// Filter Management
// ============================================================================

void BK4819_SelectFilterEx(Filter filter) {
  if (gSelectedFilter == filter) {
    return;
  }
  gSelectedFilter = filter;
  const uint16_t PIN_BIT_VHF = 0x40U >> BK4819_GPIO4_PIN32_VHF_LNA;
  const uint16_t PIN_BIT_UHF = 0x40U >> BK4819_GPIO3_PIN31_UHF_LNA;

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

  BK4819_WriteRegister(BK4819_REG_33, gGpioOutState);
}

void BK4819_SelectFilter(uint32_t frequency) {
  Filter filter =
      (frequency < SETTINGS_GetFilterBound()) ? FILTER_VHF : FILTER_UHF;

  BK4819_SelectFilterEx(filter);
}

void BK4819_SetFilterBandwidth(BK4819_FilterBandwidth_t bw) {
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

  // BK4819_WriteRegister(0x43, 0x347C); // AM 0b11010001111100
  // BK4819_WriteRegister(0x43, 0x3028); // FM 0b11000000101000
  BK4819_WriteRegister(BK4819_REG_43, value);
}

// ============================================================================
// Frequency Management
// ============================================================================

void BK4819_SetFrequency(uint32_t freq) {
  static uint16_t prev_low = 0;
  static uint16_t prev_high = 0;

  uint16_t low = freq & 0xFFFF;
  uint16_t high = (freq >> 16) & 0xFFFF;

  if (low != prev_low) {
    BK4819_WriteRegister(BK4819_REG_38, low);
    prev_low = low;
  }

  if (high != prev_high) {
    BK4819_WriteRegister(BK4819_REG_39, high);
    prev_high = high;
  }
}


void BK4819_TuneTo(uint32_t freq, bool precise) {
  BK4819_SetFrequency(freq);
  gLastFrequency = freq;

  uint16_t reg = BK4819_ReadRegister(BK4819_REG_30);

  if (precise) {
    BK4819_WriteRegister(BK4819_REG_30, 0x0200);
  } else {
    BK4819_WriteRegister(BK4819_REG_30, reg & ~BK4819_REG_30_ENABLE_VCO_CALIB);
  }

  BK4819_WriteRegister(BK4819_REG_30, reg);
}

// ============================================================================
// Modulation
// ============================================================================


void BK4819_SetAF(BK4819_AF_Type_t af) {
  BK4819_WriteRegister(BK4819_REG_47, 0x6040 | (af << 8));
}

// ============================================================================
// Squelch
// ============================================================================

void BK4819_SetupSquelch(SQL sq, uint8_t delayOpen, uint8_t delayClose) {
  sq.no = Clamp(sq.no, 0, 127);
  sq.nc = Clamp(sq.nc, 0, 127);

  BK4819_WriteRegister(BK4819_REG_4D, 0xA000 | sq.gc);
  BK4819_WriteRegister(BK4819_REG_4E, (1u << 14) | (delayOpen << 11) |
                                          (delayClose << 9) | sq.go);
  BK4819_WriteRegister(BK4819_REG_4F, (sq.nc << 8) | sq.no);
  BK4819_WriteRegister(BK4819_REG_78, (sq.ro << 8) | sq.rc);
}

void BK4819_Squelch(uint8_t sql, uint8_t openDelay, uint8_t closeDelay) {
  BK4819_SetupSquelch(GetSql(sql), openDelay, closeDelay);
}


bool BK4819_IsSquelchOpen(void) {
  return (BK4819_ReadRegister(BK4819_REG_0C) >> 1) & 1;
}

// ============================================================================
// CTCSS/CDCSS
// ============================================================================

void BK4819_SetCDCSSCodeWord(uint32_t codeWord) {
  BK4819_WriteRegister(
      BK4819_REG_51,
      BK4819_REG_51_ENABLE_CxCSS | BK4819_REG_51_GPIO6_PIN2_NORMAL |
          BK4819_REG_51_TX_CDCSS_POSITIVE | BK4819_REG_51_MODE_CDCSS |
          BK4819_REG_51_CDCSS_23_BIT | BK4819_REG_51_1050HZ_NO_DETECTION |
          BK4819_REG_51_AUTO_CDCSS_BW_ENABLE |
          BK4819_REG_51_AUTO_CTCSS_BW_ENABLE |
          (51U << BK4819_REG_51_SHIFT_CxCSS_TX_GAIN1));

  BK4819_WriteRegister(BK4819_REG_07,
                       BK4819_REG_07_MODE_CTC1 |
                           (2775U << BK4819_REG_07_SHIFT_FREQUENCY));

  BK4819_WriteRegister(BK4819_REG_08, (codeWord >> 0) & 0xFFF);
  BK4819_WriteRegister(BK4819_REG_08, 0x8000 | ((codeWord >> 12) & 0xFFF));
}

void BK4819_SetCTCSSFrequency(uint32_t freqControlWord) {
  uint16_t config = (freqControlWord == 2625) ? 0x944A : 0x904A;

  BK4819_WriteRegister(BK4819_REG_51, config);
  BK4819_WriteRegister(BK4819_REG_07, BK4819_REG_07_MODE_CTC1 |
                                          (((freqControlWord * 2065) / 1000)
                                           << BK4819_REG_07_SHIFT_FREQUENCY));
}


void BK4819_ExitSubAu(void) { BK4819_WriteRegister(BK4819_REG_51, 0x0000); }







// ============================================================================
// DTMF
// ============================================================================




void BK4819_DisableDTMF(void) { BK4819_WriteRegister(BK4819_REG_24, 0); }






// ============================================================================
// Tone Generation
// ============================================================================







// ============================================================================
// TX/RX Control
// ============================================================================

void BK4819_EnterTxMute(void) { BK4819_WriteRegister(BK4819_REG_50, 0xBB20); }

void BK4819_ExitTxMute(void) { BK4819_WriteRegister(BK4819_REG_50, 0x3B20); }

void BK4819_RX_TurnOn(void) {
  BK4819_WriteRegister(BK4819_REG_36, 0x0000);
  BK4819_WriteRegister(BK4819_REG_37, 0x1F0F);
  BK4819_WriteRegister(BK4819_REG_30, 0x0200);

  BK4819_WriteRegister(
      BK4819_REG_30,
      BK4819_REG_30_ENABLE_VCO_CALIB | BK4819_REG_30_DISABLE_UNKNOWN |
          BK4819_REG_30_ENABLE_RX_LINK | BK4819_REG_30_ENABLE_AF_DAC |
          BK4819_REG_30_ENABLE_DISC_MODE | BK4819_REG_30_ENABLE_PLL_VCO |
          BK4819_REG_30_DISABLE_PA_GAIN | BK4819_REG_30_DISABLE_MIC_ADC |
          BK4819_REG_30_DISABLE_TX_DSP | BK4819_REG_30_ENABLE_RX_DSP);
}


void BK4819_PrepareTransmit(void) {
  BK4819_ExitBypass();
  BK4819_ExitTxMute();
  BK4819_TxOn_Beep();
}

void BK4819_TxOn_Beep(void) {
  BK4819_WriteRegister(BK4819_REG_37, 0x1D0F);
  BK4819_WriteRegister(BK4819_REG_52, 0x028F);
  BK4819_Idle();
  BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);
}


void BK4819_SetupPowerAmplifier(uint8_t bias, uint32_t frequency) {
  uint8_t gain = (frequency < VHF_UHF_BOUND2) ? 0x08 : 0x22;
  BK4819_WriteRegister(BK4819_REG_36, (bias << 8) | 0x80U | gain);
}

// ============================================================================
// Bypass Mode
// ============================================================================


void BK4819_ExitBypass(void) {
  BK4819_SetAF(BK4819_AF_MUTE);
  BK4819_WriteRegister(BK4819_REG_7E, 0x302E);
}

// ============================================================================
// VOX
// ============================================================================




// ============================================================================
// Scrambler
// ============================================================================




// ============================================================================
// Crystal Oscillator
// ============================================================================


void BK4819_XtalSet(XtalMode mode) {
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

  BK4819_WriteRegister(0x3C, xtal);
  BK4819_WriteRegister(0x3D, ifset);
}

// ============================================================================
// AFC (Automatic Frequency Control)
// ============================================================================

/**
 * @param disable
 * @param range 0..7
 * @param speed 9..63
 */

#define BK4819_REG_73 0x73
#define BK4819_REG_73_DISABLE (1 << 4)
#define BK4819_REG_73_LEVEL_MASK (0xF << 11) // Mask for bits 14:11
#define BK4819_REG_73_DEFAULT_LEVEL 7        // Default for disable mode

/**
 * Set AFC level for BK4819.
 * @param level 0 = off (disable AFC), 1..8 = range
 */
void BK4819_SetAFC(uint8_t level) {
  if (level > 8) {
    level = 8;
  }

  uint16_t reg_val = BK4819_ReadRegister(BK4819_REG_73);

  reg_val &= ~(BK4819_REG_73_LEVEL_MASK | BK4819_REG_73_DISABLE);

  uint8_t afc_level;
  if (level == 0) {
    // Disable AFC with default level
    afc_level = BK4819_REG_73_DEFAULT_LEVEL;
    reg_val |= BK4819_REG_73_DISABLE;
  } else {
    // Enable AFC (disable bit remains 0) with calculated level
    afc_level = 8 - level;
  }

  reg_val |= (afc_level << 11);

  BK4819_WriteRegister(BK4819_REG_73, reg_val);
}


/**
 * Set AFC speed for BK4819.
 * @param level 0(slow)..63(fast)
 */
void BK4819_SetAFCSpeed(uint8_t speed) {
  if (speed > 63) {
    speed = 63;
  }

  uint16_t reg_val = BK4819_ReadRegister(BK4819_REG_73);

  reg_val &= ~(63 << 5);
  reg_val |= ((63 - speed) << 5);

  BK4819_WriteRegister(BK4819_REG_73, reg_val);
}


// ============================================================================
// RSSI and Signal Measurements
// ============================================================================



uint16_t BK4819_GetRSSI(void) {
  return BK4819_ReadRegister(BK4819_REG_67) & 0x1FF;
}

uint8_t BK4819_GetNoise(void) {
  return BK4819_ReadRegister(BK4819_REG_65) & 0x7F;
}

uint8_t BK4819_GetGlitch(void) {
  return BK4819_ReadRegister(BK4819_REG_63) & 0xFF;
}






// ============================================================================
// Frequency Scanning
// ============================================================================

// ============================================================================
// FSK (Frequency Shift Keying)
// ============================================================================





// ============================================================================
// Audio Control
// ============================================================================

void BK4819_ToggleAFBit(bool enable) {
  uint16_t reg = BK4819_ReadRegister(BK4819_REG_47);
  reg &= ~(1 << 8);
  if (enable) {
    reg |= 1 << 8;
  }
  BK4819_WriteRegister(BK4819_REG_47, reg);
}

void BK4819_ToggleAFDAC(bool enable) {
  uint16_t reg = BK4819_ReadRegister(BK4819_REG_30);
  reg &= ~BK4819_REG_30_ENABLE_AF_DAC;
  if (enable) {
    reg |= BK4819_REG_30_ENABLE_AF_DAC;
  }
  BK4819_WriteRegister(BK4819_REG_30, reg);
}


// ============================================================================
// Initialization
// ============================================================================

static void initialize_gpio_pins(void) {
  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
}

static void initialize_dtmf_coefficients(void) {
  for (uint8_t i = 0; i < ARRAY_SIZE(DTMF_COEFFS); ++i) {
    BK4819_WriteRegister(0x09, (i << 12) | DTMF_COEFFS[i]);
  }
}

static void initialize_registers(void) {
  // soft reset
  BK4819_WriteRegister(BK4819_REG_00, 0x8000);
  BK4819_WriteRegister(BK4819_REG_00, 0x0000);
  // power up rf
  BK4819_WriteRegister(BK4819_REG_37, 0x1D0F);

  gGpioOutState = 0x9000;
  BK4819_WriteRegister(BK4819_REG_33, gGpioOutState);

  // PA
  BK4819_SetupPowerAmplifier(0, 0);

  BK4819_WriteRegister(BK4819_REG_1E, 0x4c58); // for some revisions
  BK4819_WriteRegister(BK4819_REG_1F, 0x5454); // VCO, PLL

  BK4819_SetAGC(AUTO_GAIN_INDEX);

  BK4819_WriteRegister(BK4819_REG_3F, 0); // interrupts

  BK4819_WriteRegister(BK4819_REG_3E, 0xA037); // band sel tres
}

static void initialize_audio(void) {
  BK4819_WriteRegister(BK4819_REG_48,
                       (11u << 12) |   // Reserved
                           (0 << 10) | // AF Rx ATT-1: 0..-18dB (-6dB/step)
                           (58 << 4) | // AF Rx Gain-2 (-26..5.5 dB, 0.5dB/step)
                           (8 << 0)    // AF DAC Gain (0..15max 2dB/step)
  );
}

// A disconnected/malfunctioning BK4819 must not hang the entire boot.
static bool wait_for_crystal_stabilization(void) {
  for (unsigned elapsed = 0; elapsed < 100; ++elapsed) {
    if (!(BK4819_ReadRegister(BK4819_REG_0C) & 1U)) return true;
    BK4819_WriteRegister(BK4819_REG_02, 0);
    SYS_DelayMs(1);
  }
  return false;
}

static void configure_receiver(void) {
  BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, true);
}

static bool isInitialized = false;

void BK4819_Init(void) {
  if (isInitialized) {
    return;
  }
  gSelectedFilter = 255;
  gLastFrequency = 0;
  BK4819_InvalidateRxMode();
  initialize_gpio_pins();
  initialize_registers();
  initialize_dtmf_coefficients();
  initialize_audio();
  if (!wait_for_crystal_stabilization()) {
    BK4819_Idle();
    return;
  }
  BK4819_ConfigureMicrophone(gDeviceConfig.preferences.mic);
  configure_receiver();

  BK4819_DisableDTMF();

  // Set deviation
  BK4819_WriteRegister(0x40, (BK4819_ReadRegister(0x40) & ~(0x7FF)) |
                                 (gDeviceConfig.preferences.deviation * 10) | (1 << 12));
  isInitialized = true;
}

// Public frequency units are translated into the chip's filter selector here.
static const uint16_t bandwidthHz[] = {6000,7000,9000,10000,12000,14000,17000,20000,23000,26000};
bool BK4819_BandwidthFromHz(uint32_t hz, uint16_t *index) {
  for (uint8_t i = 0; i < ARRAY_SIZE(bandwidthHz); ++i) {
    if (bandwidthHz[i] == hz) {
      *index = i;
      return true;
    }
  }
  return false;
}
