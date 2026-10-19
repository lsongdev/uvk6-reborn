#include "hal/systick.h"
#include "hal/scheduler.h"
#include "hal/system.h"
#include "py32f0xx.h"

// SysTick at 1 kHz counts milliseconds; microsecond delays poll its counter.
#define TICKS_PER_US (CPU_CLOCK_HZ / 1000000u)
#define TICKS_PER_MS (CPU_CLOCK_HZ / 1000u)

static volatile uint32_t uptimeMs;

void SystickHandler(void) { ++uptimeMs; }

void TIMER0_InitAsUptimeCounter(void) {
  SysTick->LOAD = TICKS_PER_MS - 1u;
  SysTick->VAL = 0;
  NVIC_SetPriority(SysTick_IRQn, 0);
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

void TIMER1_InitForDelay(void) {}

void TIMER_DelayTicks(uint32_t ticks) {
  uint32_t previous = SysTick->VAL, elapsed = 0;
  while (elapsed < ticks) {
    uint32_t now = SysTick->VAL;
    // The counter runs down and reloads at zero.
    elapsed += now <= previous ? previous - now : previous + TICKS_PER_MS - now;
    previous = now;
  }
}

void TIMER_DelayUs(uint32_t us) {
  // Long delays in 1 ms steps keep the tick arithmetic within 32 bits.
  while (us >= 1000u) {
    TIMER_DelayTicks(TICKS_PER_MS);
    us -= 1000u;
  }
  TIMER_DelayTicks(us * TICKS_PER_US);
}

uint32_t GetUptimeMs(void) { return uptimeMs; }

uint32_t Now(void) { return uptimeMs; }
