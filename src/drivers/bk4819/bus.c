#include "drivers/bk4819/bk4819.h"
#include "drivers/bk4819/internal.h"
#include "hal/gpio.h"
#include "hal/systick.h"

static inline void bus_delay(void) {
  // BK4829 reference uses 40 NOPs at 48 MHz. Use at least 1 us on K1;
  // retain the measured K6 timing. All selection stays board-provided.
  if (isBk4829()) TIMER_DelayUs(1);
  else __asm volatile("nop\n nop\n nop\n nop\n nop\n"
                      "nop\n nop\n nop\n nop\n nop\n"
                      "nop\n nop\n nop\n");
}
static inline void select_delay(void) {
  if (isBk4829()) TIMER_DelayUs(1);
  else __asm volatile("nop\n nop\n");
}
#define SHORT_DELAY() bus_delay()

static inline void gpio_set_scn(bool high) { GPIO_Write(GPIO_BK4819_SCN, high); }
static inline void gpio_set_scl(bool high) { GPIO_Write(GPIO_BK4819_SCL, high); }
static inline void gpio_set_sda(bool high) { GPIO_Write(GPIO_BK4819_SDA, high); }

static void spi_write_byte(uint8_t data) {
  gpio_set_scl(false);

  for (uint8_t i = 0; i < 8; i++) {
    gpio_set_sda(data & 0x80);
    SHORT_DELAY();
    gpio_set_scl(true);
    SHORT_DELAY();
    data <<= 1;
    gpio_set_scl(false);
    SHORT_DELAY();
  }
}

static uint16_t spi_read_word(void) {
  GPIO_SetInput(GPIO_BK4819_SDA, true);
  TIMER_DelayUs(1);

  uint16_t value = 0;
  for (uint8_t i = 0; i < 16; i++) {
    value <<= 1;
    value |= GPIO_Read(GPIO_BK4819_SDA);
    gpio_set_scl(true);
    SHORT_DELAY();
    gpio_set_scl(false);
    SHORT_DELAY();
  }

  GPIO_SetInput(GPIO_BK4819_SDA, false);

  return value;
}

static void spi_write_word(uint16_t data) {
  gpio_set_scl(false);

  for (uint8_t i = 0; i < 16; i++) {
    gpio_set_sda(data & 0x8000);
    SHORT_DELAY();
    gpio_set_scl(true);
    data <<= 1;
    SHORT_DELAY();
    gpio_set_scl(false);
    SHORT_DELAY();
  }
}

// ============================================================================
// Register Access
// ============================================================================

static uint16_t reg30state = 0xffff;

uint16_t BK4819_ReadRegister(BK4819_REGISTER_t reg) {
  if (reg == BK4819_REG_30 && reg30state != 0xffff) {
    return reg30state;
  }
  gpio_set_scn(true);
  gpio_set_scl(false);
  select_delay();
  gpio_set_scn(false);

  spi_write_byte(reg | 0x80);
  uint16_t value = spi_read_word();

  gpio_set_scn(true);
  gpio_set_scl(true);
  gpio_set_sda(true);

  return value;
}

void BK4819_WriteRegister(BK4819_REGISTER_t reg, uint16_t data) {
  if (reg == BK4819_REG_00 && (data & 0x8000u)) reg30state = 0xffff;
  if (reg == BK4819_REG_30) {
    reg30state = data;
  }
  gpio_set_scn(true);
  gpio_set_scl(false);
  select_delay();
  gpio_set_scn(false);

  spi_write_byte(reg);
  spi_write_word(data);

  gpio_set_scn(true);
  gpio_set_scl(true);
}


void BK4819_BusInit(void) {
  gpio_set_scn(true);
  gpio_set_scl(true);
  gpio_set_sda(true);
}

