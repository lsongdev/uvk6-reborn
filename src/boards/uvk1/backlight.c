#include "hal/board.h"
#include "boards/uvk1/pins.h"
#include "hal/system.h"
#include "hal/systick.h"

// PF8 has no timer output: TIM7 update requests drive BSRR through DMA7.
// Request 39 is TIM7_UP in the PY32F071 DMA map (Puya LL system header).
enum { PWM_SLOTS = 32, PWM_HZ = 4000, TIM7_UP_REQUEST = 39 };
static uint32_t dutyCycle[PWM_SLOTS];

void BOARD_BacklightInit(void) {
  RCC->AHBENR |= RCC_AHBENR_DMAEN;
  RCC->APBENR1 |= RCC_APBENR1_TIM7EN;
  RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
  RCC->APBRSTR1 |= RCC_APBRSTR1_TIM7RST;
  RCC->APBRSTR1 &= ~RCC_APBRSTR1_TIM7RST;
  DMA1_Channel7->CCR = 0;
  SYSCFG->CFGR4 = (SYSCFG->CFGR4 & ~SYSCFG_CFGR4_DMA7_MAP) |
                 (TIM7_UP_REQUEST << SYSCFG_CFGR4_DMA7_MAP_Pos);
  TIM7->PSC = 0;
  TIM7->ARR = CPU_CLOCK_HZ / PWM_HZ / PWM_SLOTS - 1u;
  TIM7->CR1 = TIM_CR1_ARPE;
  TIM7->EGR = TIM_EGR_UG;
  TIM7->DIER = TIM_DIER_UDE;
  DMA1_Channel7->CPAR = (uint32_t)(uintptr_t)&GPIOF->BSRR;
  DMA1_Channel7->CMAR = (uint32_t)(uintptr_t)dutyCycle;
  DMA1_Channel7->CCR = DMA_CCR_DIR | DMA_CCR_CIRC | DMA_CCR_MINC |
                      DMA_CCR_PSIZE_1 | DMA_CCR_MSIZE_1 | DMA_CCR_PL_1;
  GPIOF->BRR = 1u << UVK1_BACKLIGHT;
}

void BOARD_BacklightSetLevel(uint8_t level) {
  // Distinct, progressively spaced duty cycles for every user level.
  static const uint8_t slots[] = {0,1,2,3,4,5,7,9,11,14,17,20,23,26,29,32};
  if (level > 15) level = 15;
  TIM7->CR1 &= ~TIM_CR1_CEN;
  DMA1_Channel7->CCR &= ~DMA_CCR_EN;
  // Let an already issued DMA write finish before changing the GPIO/buffer.
  TIMER_DelayUs(1);
  if (level == 0 || level == 15) {
    GPIOF->BSRR = 1u << (UVK1_BACKLIGHT + (level ? 0u : 16u));
    return;
  }
  for (unsigned i = 0; i < PWM_SLOTS; ++i)
    dutyCycle[i] = 1u << (UVK1_BACKLIGHT + (i < slots[level] ? 0u : 16u));
  DMA1->IFCR = DMA_IFCR_CGIF7;
  DMA1_Channel7->CNDTR = PWM_SLOTS;
  TIM7->CNT = 0;
  DMA1_Channel7->CCR |= DMA_CCR_EN;
  TIM7->CR1 |= TIM_CR1_CEN;
}
