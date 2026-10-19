#include "hal/board.h"
#include "hal/gpio.h"
#include "hal/system.h"
#include "hal/systick.h"
#include "hal/crc.h"
#include "hal/uart.h"
#include "drivers/py32f071/usb.h"
#include "hal/i2c.h"
#include "boards/uvk1/pins.h"
#include "drivers/py32f071/adc.h"

static void output(uint8_t port, uint8_t pin, bool high) {
  // Load the latch before enabling the output to avoid select/audio glitches.
  PY32_Port(port)->BSRR = 1u << (pin + (high ? 0u : 16u));
  PY32_PinMode(port, pin, PY32_OUTPUT, PY32_PULL_NONE);
}

void BOARD_Init(void) {
  SYS_ConfigureClocks();
  SYS_ConfigureSysCon();
  output(PY32_PORT_F, UVK1_BACKLIGHT, false);
  output(PY32_PORT_A, UVK1_FLASH_CS, true);
  output(PY32_PORT_A, 8, false); // Speaker muted.
  output(PY32_PORT_A, 6, true);
  output(PY32_PORT_B, 2, true);
  output(PY32_PORT_B, 8, true);
  output(PY32_PORT_B, 9, true);
  output(PY32_PORT_F, 9, true);
  output(PY32_PORT_C, 13, false); // Flashlight off.
  for (uint8_t pin = 3; pin <= 6; ++pin) output(PY32_PORT_B, pin, true);
  for (uint8_t pin = 12; pin <= 15; ++pin)
    PY32_PinMode(PY32_PORT_B, pin, PY32_INPUT, PY32_PULL_UP);
  PY32_PinMode(PY32_PORT_B, 10, PY32_INPUT, PY32_PULL_UP);
  PY32_PinMode(PY32_PORT_B, 0, PY32_ANALOG, PY32_PULL_NONE);
  PY32_PinMode(PY32_PORT_B, 1, PY32_ANALOG, PY32_PULL_NONE);
  // LCD SPI1 and flash SPI2. Keep SWD and USB pins untouched.
  PY32_PinAlternate(PY32_PORT_A, 5, 0, PY32_PULL_UP);
  PY32_PinAlternate(PY32_PORT_A, 7, 0, PY32_PULL_UP);
  PY32_PinAlternate(PY32_PORT_A, 0, 8, PY32_PULL_UP);
  PY32_PinAlternate(PY32_PORT_A, 1, 9, PY32_PULL_UP);
  PY32_PinAlternate(PY32_PORT_A, 2, 9, PY32_PULL_UP);
  PY32_PinAlternate(PY32_PORT_A, 9, 1, PY32_PULL_UP);
  PY32_PinAlternate(PY32_PORT_A, 10, 1, PY32_PULL_UP);
  TIMER0_InitAsUptimeCounter(); // Start delays before I2C initialization.
  TIMER1_InitForDelay();
  PY32_ADC_Init();
  CRC_Init();
  UART_Init();
  USB_Init();
  I2C_Init();
}

void BOARD_ADC_GetBatteryInfo(uint16_t *voltage, uint16_t *current) {
  *voltage = PY32_ADC_Read(8);
  *current = 0; // Reference K1 firmware exposes no charging-current measurement.
}


// Reference FM_Start selects the VHF LNA and stops dual watch (5fedc09).
bool BOARD_BroadcastUsesBkVhfPath(void) { return true; }


bool BOARD_HasHorizontalArrows(void) { return true; }

// The K-plug UART and the USB-C port carry the same PC link.
const SerialPort *const BOARD_SERIAL_PORTS[] = {&UART_PORT, &USB_PORT};
const uint8_t BOARD_SERIAL_PORT_COUNT = 2;
