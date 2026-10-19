#include "hal/board.h"
#include "hal/system.h"
#include "drivers/mcu/dp32g030/portcon.h"
#include "drivers/mcu/dp32g030/pwmplus.h"

// PB6 driven by PWM_PLUS0 channel 0 at 25 kHz, 10-bit period.
void BOARD_BacklightInit(void) {
  const uint32_t PWM_FREQUENCY_HZ = 25000;
  PWM_PLUS0_CLKSRC |= ((CPU_CLOCK_HZ / 1024 / PWM_FREQUENCY_HZ) << 16);
  PWM_PLUS0_PERIOD = 1023;

  PORTCON_PORTB_SEL0 &= ~(0
                          // Back light
                          | PORTCON_PORTB_SEL0_B6_MASK);
  PORTCON_PORTB_SEL0 |= 0
                        // Back light PWM
                        | PORTCON_PORTB_SEL0_B6_BITS_PWMP0_CH0;

  PWM_PLUS0_GEN =
      PWMPLUS_GEN_CH0_OE_BITS_ENABLE | PWMPLUS_GEN_CH0_OUTINV_BITS_ENABLE | 0;

  PWM_PLUS0_CFG =
      PWMPLUS_CFG_CNT_REP_BITS_ENABLE | PWMPLUS_CFG_COUNTER_EN_BITS_ENABLE | 0;
}

void BOARD_BacklightSetLevel(uint8_t level) { PWM_PLUS0_CH0_COMP = (1 << (level + 2)) - 1; }
