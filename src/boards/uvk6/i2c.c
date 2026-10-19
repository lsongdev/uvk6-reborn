#include "hal/i2c.h"
#include "external/CMSIS_5/Device/ARM/ARMCM0/Include/ARMCM0.h"
#include "drivers/mcu/dp32g030/gpio.h"
#include "drivers/mcu/dp32g030/portcon.h"
#include "boards/uvk6/pins.h"
#include "hal/systick.h"

// Clock-stretch timeout is sticky until the next START.
static bool bus_error;

static inline void i2c_delay_short(void) {
  __asm volatile("nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n");
}

static inline void i2c_delay_long(void) {
  __asm volatile("nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n nop\n nop\n"
                 "nop\n nop\n nop\n");
}

/* === Управление состоянием SDA === */

/* SDA -> Output (для записи) */
static inline void sda_out(void) {
  PORTCON_PORTA_IE &= ~PORTCON_PORTA_IE_A11_MASK;
  PORTCON_PORTA_OD |= PORTCON_PORTA_OD_A11_BITS_ENABLE;
  GPIOA->DIR |= GPIO_DIR_11_BITS_OUTPUT;
}

/* SDA -> Input (для чтения) */
static inline void sda_in(void) {
  GPIO_SetBit(&GPIOA->DATA, GPIOA_PIN_I2C_SDA); // Отпускаем линию
  PORTCON_PORTA_IE |= PORTCON_PORTA_IE_A11_BITS_ENABLE;
  GPIOA->DIR &= ~GPIO_DIR_11_MASK;
}

/* Установить SDA = 0 */
static inline void sda_lo(void) {
  GPIO_ClearBit(&GPIOA->DATA, GPIOA_PIN_I2C_SDA);
}

/* Установить SDA = 1 */
static inline void sda_hi(void) {
  GPIO_SetBit(&GPIOA->DATA, GPIOA_PIN_I2C_SDA);
}

/* Прочитать SDA */
static inline bool sda_read(void) {
  return GPIO_CheckBit(&GPIOA->DATA, GPIOA_PIN_I2C_SDA);
}

static inline bool scl_read(void) {
  return GPIO_CheckBit(&GPIOA->DATA, GPIOA_PIN_I2C_SCL);
}

/* === Управление SCL === */

static inline void scl_lo(void) {
  GPIO_ClearBit(&GPIOA->DATA, GPIOA_PIN_I2C_SCL);
}

static inline void scl_hi(void) {
  GPIO_SetBit(&GPIOA->DATA, GPIOA_PIN_I2C_SCL);
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

void I2C_Init(void) {
  /* Clock-stretch polling needs SCL's input buffer even in output mode. */
  PORTCON_PORTA_IE |= PORTCON_PORTA_IE_A10_BITS_ENABLE;

  /* Open-drain для обеих линий */
  PORTCON_PORTA_OD |=
      PORTCON_PORTA_OD_A10_BITS_ENABLE | PORTCON_PORTA_OD_A11_BITS_ENABLE;

  /* Оба пина как выходы */
  GPIOA->DIR |= GPIO_DIR_10_BITS_OUTPUT | GPIO_DIR_11_BITS_OUTPUT;

  /* Idle state: обе линии HIGH */
  scl_hi();
  sda_hi();

  TIMER_DelayUs(10);
}

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
