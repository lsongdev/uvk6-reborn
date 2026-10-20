// Run the actual board drivers against register storage, including DMA PWM.
#include <assert.h>
#include <stdint.h>
#if TEST_UVK1
#include "boards/uvk1/pins.h"
static RCC_TypeDef rcc;
static SYSCFG_TypeDef syscfg;
static TIM_TypeDef timer;
static DMA_TypeDef dma;
static DMA_Channel_TypeDef channel;
static GPIO_TypeDef gpio;
#undef RCC
#undef SYSCFG
#undef TIM7
#undef DMA1
#undef DMA1_Channel7
#undef GPIOF
#define RCC (&rcc)
#define SYSCFG (&syscfg)
#define TIM7 (&timer)
#define DMA1 (&dma)
#define DMA1_Channel7 (&channel)
#define GPIOF (&gpio)
void TIMER_DelayUs(uint32_t us) { assert(us == 1); }
#include "../src/boards/uvk1/backlight.c"
#else
#include "drivers/dp32g030/pwmplus.h"
#include "drivers/dp32g030/portcon.h"
static uint32_t clockSource, period, selectPins, gen, cfg, compare;
#undef PWM_PLUS0_CLKSRC
#undef PWM_PLUS0_PERIOD
#undef PWM_PLUS0_GEN
#undef PWM_PLUS0_CFG
#undef PWM_PLUS0_CH0_COMP
#undef PORTCON_PORTB_SEL0
#define PWM_PLUS0_CLKSRC clockSource
#define PWM_PLUS0_PERIOD period
#define PWM_PLUS0_GEN gen
#define PWM_PLUS0_CFG cfg
#define PWM_PLUS0_CH0_COMP compare
#define PORTCON_PORTB_SEL0 selectPins
#include "../src/boards/uvk6/backlight.c"
#endif

int main(void) {
  BOARD_BacklightInit();
#if TEST_UVK1
  assert(timer.ARR == 374 && timer.PSC == 0);
  assert((syscfg.CFGR4 & SYSCFG_CFGR4_DMA7_MAP) == (39u << 16));
#else
  assert(period == 1023);
#endif
  // Both traversal directions, including transitions from PWM to off/full.
  for (unsigned direction = 0; direction < 2; ++direction) {
    unsigned previous = 0;
    for (unsigned i = 0; i < 16; ++i) {
      unsigned level = direction ? 15 - i : i;
      BOARD_BacklightSetLevel(level);
      unsigned duty;
#if TEST_UVK1
      duty = 0;
      if (level == 0 || level == 15) {
        assert(!(timer.CR1 & TIM_CR1_CEN) && !(channel.CCR & DMA_CCR_EN));
        assert(gpio.BSRR == (1u << (UVK1_BACKLIGHT + (level ? 0u : 16u))));
        duty = level ? 32 : 0;
      } else {
        assert(timer.CR1 & TIM_CR1_CEN);
        assert(channel.CCR & DMA_CCR_EN);
        assert(channel.CNDTR == 32);
        for (unsigned slot = 0; slot < 32; ++slot) {
          assert(dutyCycle[slot] == (1u << UVK1_BACKLIGHT) ||
                 dutyCycle[slot] == (1u << (UVK1_BACKLIGHT + 16u)));
          duty += dutyCycle[slot] == (1u << UVK1_BACKLIGHT);
        }
      }
#else
      duty = compare;
      assert(duty <= period);
      if (!level) assert(!duty);
      if (level == 15) assert(duty == period);
#endif
      if (i) assert(direction ? duty < previous : duty > previous);
      previous = duty;
    }
  }
  return 0;
}
