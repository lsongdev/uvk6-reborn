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
  EEPROM_Init();
  printf("UVK6 Reborn\n");

  SYS_Main();
}
