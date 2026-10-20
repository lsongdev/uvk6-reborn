// Trace the LCD command/data pin, CS and SPI writes for live contrast changes.
#include "drivers/st7565/st7565.h"
#include "hal/gpio.h"
#include "hal/spi.h"
#include "hal/system.h"
#include "hal/systick.h"
#include <assert.h>
static bool selected, dataMode;
static uint8_t commands[32];
static unsigned count, resets;
void GPIO_Write(GpioLine line, bool value) {
  if (line == GPIO_ST7565_A0) dataMode = value;
  if (line == GPIO_ST7565_RES) ++resets;
}
void SPI_Init(void) { assert(0); }
void SPI_Select(bool on) { selected = on; }
void SPI_Write(uint8_t value) {
  assert(selected && !dataMode && count < sizeof(commands));
  commands[count++] = value;
}
void SPI_Flush(void) {}
void TIMER_DelayUs(uint32_t us) { (void)us; assert(0); }
void SYS_DelayMs(uint32_t ms) { (void)ms; assert(0); }
int main(void) {
  for (unsigned level = 0; level < 16; ++level) {
    count = 0;
    ST7565_Init(false, level);
    assert(!selected && !resets && count == 10);
    assert(commands[5] == 0x24); // Keep resistor ratio fixed.
    assert(commands[6] == 0x81 && commands[7] == 23 + level);
    assert(commands[9] == 0xAF);
  }
  return 0;
}
