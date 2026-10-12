#include "hal/board.h"
#include "hal/crc.h"
#include "hal/eeprom.h"
#include "hal/gpio.h"
#include "hal/i2c.h"
#include "hal/system.h"
#include "hal/systick.h"
#include "hal/uart.h"
#include "external/printf/printf.h"
#include "drivers/dp32g030/gpio.h"
#include "apps/system.h"

static const char *APP_NAME = "hawk5";

void Main(void) {
  SYS_ConfigureClocks();
  SYS_ConfigureSysCon();

  BOARD_GPIO_Init();
  BOARD_PORTCON_Init();
  BOARD_ADC_Init();

  CRC_Init();
  UART_Init();
  I2C_Init();

  TIMER0_InitAsUptimeCounter();
  TIMER1_InitForDelay();
  /* for (;;) {
    printf("Tick %u\n", (uint32_t)GetUptimeMs());
    for (int i = 0; i < 10; ++i)
      TIMER_DelayUs(1000);
  } */

  EEPROM_Init(); // Автоматически определит параметры
  /* EEPROM_TestReadSpeedFast();
  for (;;) {
  } */
  /* EEPROM_ScanBus();
  for (;;) {
  } */

  printf("hawk5\n");

  SYS_Main();
}
