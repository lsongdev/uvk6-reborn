#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

/* ----------  hardware abstraction macros  ---------- */
// Инициализация
void TIMER0_InitAsUptimeCounter(void); // Основной uptime
void TIMER1_InitForDelay(void);        // Только задержки

// Задержки
void TIMER_DelayUs(uint32_t us);
void TIMER_DelayTicks(uint32_t ticks);

// Uptime (время с момента включения)
uint32_t GetUptimeMs(void);  // миллисекунды

#endif
