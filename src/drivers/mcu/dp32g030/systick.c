#include "hal/systick.h"
#include "external/CMSIS_5/Device/ARM/ARMCM0/Include/ARMCM0.h"
#include "drivers/mcu/dp32g030/timer.h"

#define TIMER0_BASE 0x40064000
#define TIMER1_BASE 0x40064800

typedef struct {
  volatile uint32_t EN;        // 0x00
  volatile uint32_t DIV;       // 0x04
  uint32_t _rsv[2];            // 0x08-0x0C (8 байт)
  volatile uint32_t IE;        // 0x10
  volatile uint32_t IF;        // 0x14
  uint32_t _rsv2[2];           // 0x18-0x1C (8 байт) ← ВОТ ОНО!
  volatile uint32_t HIGH_LOAD; // 0x20
  volatile uint32_t HIGH_CNT;  // 0x24
  uint32_t _rsv3[2];           // 0x28-0x2C (8 байт) ← И ЭТО!
  volatile uint32_t LOW_LOAD;  // 0x30
  volatile uint32_t LOW_CNT;   // 0x34
} TIMERBASE_TypeDef;

#define TIMER0 ((TIMERBASE_TypeDef *)TIMER0_BASE)
#define TIMER1 ((TIMERBASE_TypeDef *)TIMER1_BASE)

static inline void timer_base_enable_clk(void) {
  /* SYSCON->DEV_CLK_GATE bit 12 & 13 */
  *(volatile uint32_t *)0x40000008 |= (3UL << 12);
}

/* 48 MHz PCLK  →  DIV = 47  gives exactly 1 MHz tick (1 µs) */
#define TICKS_PER_US 1
#define TICKS_PER_MS 1000
#define TICKS_PER_SEC 1000000
void TIMER0_InitAsUptimeCounter(void) {
  TIMER0->DIV = 47;
  TIMER0->HIGH_LOAD = 0xFFFF;
  TIMER0->IF = 2;
  TIMER0->IE = 2;
  NVIC_EnableIRQ(5);
  TIMER0->EN = 0x00000002;
}
static volatile uint32_t uptime_ms = 0;
static volatile uint16_t accum_us = 0;
void HandlerTIMER_BASE0(void) {
  if (TIMER0->IF & 2) {
    TIMER0->IF = 2;
    uptime_ms += 65;
    accum_us += 536;
    if (accum_us >= 1000) {
      uptime_ms += 1;
      accum_us -= 1000;
    }
  }
}
uint32_t GetUptimeMs(void) {
  uint32_t ms1, ms2;
  uint16_t acc1, acc2;
  uint32_t cnt;
  do {
    ms1 = uptime_ms;
    acc1 = accum_us;
    cnt = TIMER0->HIGH_CNT;
    ms2 = uptime_ms;
    acc2 = accum_us;
  } while (ms1 != ms2 || acc1 != acc2);
  uint32_t sub = acc1 + cnt;
  return ms1 + sub / 1000;
}
void TIMER1_InitForDelay(void) {
  TIMER1->EN = 0;
  TIMER1->IE = 0;
  TIMER1->IF = 3;
  TIMER1->DIV = 47;
}
static void timer1_wait_ticks(uint32_t ticks) {
  if (ticks == 0)
    return;
  if (ticks > 0x10000)
    ticks = 0x10000; /* max 65536 */
  TIMER1->EN = 0;
  TIMER1->IF = 1;
  TIMER1->LOW_LOAD = ticks - 1;
  TIMER1->EN = 0x00000001;
  /* Ждать переполнения LOW (бит 0 в IF) */
  while (!(TIMER1->IF & 1))
    ;
  TIMER1->EN = 0;
  TIMER1->IF = 1;
}
void TIMER_DelayTicks(uint32_t ticks) {
  /* Разбиваем на куски по 65536 тиков */
  while (ticks > 0x10000) {
    timer1_wait_ticks(0x10000);
    ticks -= 0x10000;
  }
  if (ticks > 0) {
    timer1_wait_ticks(ticks);
  }
}
void TIMER_DelayUs(uint32_t us) { TIMER_DelayTicks(us); }
