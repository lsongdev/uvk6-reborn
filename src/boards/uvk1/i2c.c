#include "hal/i2c.h"
#include "hal/systick.h"
#include "boards/uvk1/pins.h"
static bool bus_error;
static inline void i2c_delay_short(void) { TIMER_DelayUs(2); }
static inline void i2c_delay_long(void) { TIMER_DelayUs(3); }
static inline void sda_lo(void) { GPIOF->BRR = 1u << UVK1_I2C_SDA; }
static inline void sda_hi(void) { GPIOF->BSRR = 1u << UVK1_I2C_SDA; }
static inline void sda_out(void) {} // Open drain always; high releases SDA.
static inline void sda_in(void) { sda_hi(); }
static inline bool sda_read(void) { return GPIOF->IDR & (1u << UVK1_I2C_SDA); }
static inline void scl_lo(void) { GPIOF->BRR = 1u << UVK1_I2C_SCL; }
static inline void scl_hi(void) { GPIOF->BSRR = 1u << UVK1_I2C_SCL; }
static inline bool scl_read(void) { return GPIOF->IDR & (1u << UVK1_I2C_SCL); }
void I2C_Init(void) {
  scl_hi();
  sda_hi();
  PY32_PinMode(PY32_PORT_F, UVK1_I2C_SCL, PY32_INPUT, PY32_PULL_UP);
  PY32_PinMode(PY32_PORT_F, UVK1_I2C_SDA, PY32_INPUT, PY32_PULL_UP);
  GPIOF->OTYPER |= (1u << UVK1_I2C_SCL) | (1u << UVK1_I2C_SDA);
  GPIOF->MODER = (GPIOF->MODER & ~((3u << 10) | (3u << 12))) |
                 (1u << 10) | (1u << 12);
  bus_error = false;
  TIMER_DelayUs(10);
}

static inline bool scl_hi_wait(void) {
  scl_hi();

  for (uint32_t elapsed = 0; elapsed < 10000; ++elapsed) {
    if (scl_read()) {
      return true;
    }
    TIMER_DelayUs(1);
  }
  return false;
}

/* === Основные функции I2C === */

void I2C_Start(void) {
  bus_error = false;
  sda_out();
  sda_hi();
  if (!scl_hi_wait()) {
    bus_error = true;
    return;
  }
  i2c_delay_short(); // Setup time для START (600 нс)
  sda_lo();          // SDA падает при SCL=HIGH
  i2c_delay_short(); // Hold time для START (600 нс)
  scl_lo();
  i2c_delay_long();
}

void I2C_Stop(void) {
  sda_out();
  sda_lo();
  i2c_delay_long();
  scl_hi();
  i2c_delay_short(); // Setup time для STOP (600 нс)
  sda_hi();          // SDA поднимается при SCL=HIGH
  i2c_delay_long();  // Bus free time
}

void I2C_RepStart(void) {
  sda_out();
  scl_lo(); // Убедимся что SCL = LOW
  i2c_delay_long();
  sda_hi(); // SDA поднимается при LOW SCL
  i2c_delay_short();
  if (!scl_hi_wait()) {
    bus_error = true;
    return;
  }
  i2c_delay_short();
  sda_lo(); // SDA падает при HIGH SCL = Repeated START
  i2c_delay_short();
  scl_lo();
  i2c_delay_long();
}

int I2C_Write(uint8_t Data) {
  int ret = -1;
  if (bus_error) return -1;
  sda_out();
  scl_lo();
  i2c_delay_long(); // Добавить задержку LOW периода

  for (uint8_t i = 0; i < 8; i++) {
    ((Data & 0x80) ? sda_hi : sda_lo)();
    Data <<= 1;

    i2c_delay_short(); // Setup time для данных (100 нс минимум)
    if (!scl_hi_wait()) {
      bus_error = true;
      scl_lo();
      return -1;
    }
    i2c_delay_short(); // HIGH период SCL
    scl_lo();
    i2c_delay_long(); // LOW период SCL
  }

  // Читаем ACK
  sda_in();
  if (!scl_hi_wait()) {
    bus_error = true;
    scl_lo();
    return -1;
  }
  i2c_delay_short();

  if (!sda_read()) {
    ret = 0; // ACK получен
  }

  scl_lo();
  return ret;
}

uint8_t I2C_Read(bool bFinal) {
  uint8_t Data = 0;
  if (bus_error) return 0;
  sda_in();

  for (uint8_t i = 0; i < 8; i++) {
    Data <<= 1;

    if (!scl_hi_wait()) {
      bus_error = true;
      scl_lo();
      return 0;
    }
    i2c_delay_short();

    if (sda_read()) {
      Data |= 1;
    }
    i2c_delay_short(); // tHIGH

    scl_lo();
    i2c_delay_long();
  }

  // ACK/NACK
  sda_out();
  (bFinal ? sda_hi : sda_lo)();

  if (!scl_hi_wait()) bus_error = true;
  i2c_delay_short();
  scl_lo();

  return Data;
}

/* === Вспомогательные функции === */

uint16_t I2C_ReadBuffer(void *pBuffer, uint16_t Size) {
  uint8_t *pData = (uint8_t *)pBuffer;

  if (Size == 0)
    return 0;

  for (uint16_t i = 0; i < Size; ++i) {
    pData[i] = I2C_Read(i + 1u == Size);
    if (bus_error) return i;
  }
  return Size;
}

int I2C_WriteBuffer(const uint8_t *pBuffer, uint16_t Size) {
  if (!pBuffer && Size)
    return -1;
  for (uint16_t i = 0; i < Size; ++i) {
    // A NACK invalidates this transaction. Repeating the byte in the same
    // transaction is not an EEPROM/Si47xx command retry.
    if (I2C_Write(pBuffer[i]) != 0)
      return -1;
  }
  return 0;
}
