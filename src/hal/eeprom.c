#include "hal/eeprom.h"
#include "hal/eeprom-layout.h"
#include "external/printf/printf.h"
#include "hal/scheduler.h"
#include "hal/i2c.h"
#include "hal/system.h"
#include "hal/systick.h"
#include <string.h>

bool gEepromWrite = false;
static bool writes_enabled = false;

void EEPROM_SetWritesEnabled(bool enabled) { writes_enabled = enabled; }

static uint32_t g_eeprom_size = 0;
static uint16_t g_eeprom_page_size = 0;

void EEPROM_Init(void) {
  g_eeprom_size = EEPROM_DetectSize();
  g_eeprom_page_size = EEPROM_GetPageSize();
  printf("EEPROM: %lu bytes, page %u bytes\n", g_eeprom_size,
         g_eeprom_page_size);
}

bool EEPROM_WaitReady(uint8_t device_addr, uint32_t timeout_ms) {
  uint32_t start = Now();
  while ((Now() - start) < timeout_ms) {
    I2C_Start();
    int result = I2C_Write(device_addr);
    I2C_Stop();
    if (result == 0)
      return true;
    TIMER_DelayUs(100);
  }
  return false;
}

// Оптимизированное чтение с Sequential Read
// Автоматически определяет границы чипов и читает максимальными блоками
int EEPROM_ReadBufferSequential(uint32_t address, void *pBuffer,
                                uint16_t size) {
  if (size == 0)
    return 0;
  if (!pBuffer || address >= g_eeprom_size || size > g_eeprom_size - address)
    return -1;

  uint8_t *pData = (uint8_t *)pBuffer;

  while (size > 0) {
    // Определяем границу текущего чипа (64KB для большинства EEPROM)
    uint32_t chip_boundary =
        (address & ~0xFFFFu) + 0x10000u; // Next 64-KiB bank
    uint32_t bytes_to_boundary = chip_boundary - address;
    uint16_t chunk_size = (size < bytes_to_boundary) ? size : bytes_to_boundary;

    // Адрес устройства с учетом старших битов адреса
    uint8_t IIC_ADD = 0xA0 | ((address >> 15) & 0x0E);

    // Отправляем адрес для чтения
    I2C_Start();
    if (I2C_Write(IIC_ADD) != 0) {
      I2C_Stop();
      return -1;
    }
    if (I2C_Write((address >> 8) & 0xFF) != 0) {
      I2C_Stop();
      return -1;
    }
    if (I2C_Write(address & 0xFF) != 0) {
      I2C_Stop();
      return -1;
    }

    // Repeated START для чтения
    I2C_RepStart();
    if (I2C_Write(IIC_ADD | 0x01) != 0) {
      I2C_Stop();
      return -1;
    }

    // Sequential Read - читаем весь блок за раз
    uint16_t read_size = I2C_ReadBuffer(pData, chunk_size);
    I2C_Stop();
    if (read_size != chunk_size)
      return -1;

    pData += chunk_size;
    address += chunk_size;
    size -= chunk_size;
  }

  return 0;
}

// Обновляем основную функцию чтения
int EEPROM_ReadBuffer(uint32_t address, void *pBuffer, uint16_t size) {
  return EEPROM_ReadBufferSequential(address, pBuffer, size);
}

/* int EEPROM_ReadBuffer(uint32_t address, void *pBuffer, uint16_t size) {
  if (size == 0)
    return 0;

  address &= 0x3FFFF;
  uint8_t IIC_ADD = 0xA0 | ((address >> 15) & 0x0E);

  I2C_Start();
  if (I2C_Write(IIC_ADD) != 0) {
    I2C_Stop();
    return -1;
  }
  if (I2C_Write((address >> 8) & 0xFF) != 0) {
    I2C_Stop();
    return -1;
  }
  if (I2C_Write(address & 0xFF) != 0) {
    I2C_Stop();
    return -1;
  }

  I2C_RepStart();
  if (I2C_Write(IIC_ADD | 0x01) != 0) {
    I2C_Stop();
    return -1;
  }

  I2C_ReadBuffer(pBuffer, size);
  I2C_Stop();
  return 0;
} */

bool EEPROM_TryWriteBuffer(uint32_t address, const uint8_t *pBuffer, uint16_t size) {
  if (!writes_enabled || pBuffer == NULL ||
      !EEPROM_WriteAllowed(address, size) ||
      address >= g_eeprom_size || size > g_eeprom_size - address)
    return false;

  uint16_t PAGE_SIZE = EEPROM_GetPageSize();
  if (!PAGE_SIZE)
    return false;
  // ACK-count detection does not identify the physical part or its page size.
  // Use a conservative 32-byte maximum until the actual EEPROM is verified.
  if (PAGE_SIZE > 32)
    PAGE_SIZE = 32;

  while (size > 0) {
    uint16_t page_offset = address % PAGE_SIZE;
    uint16_t chunk_size =
        (size < (PAGE_SIZE - page_offset)) ? size : (PAGE_SIZE - page_offset);
    uint8_t IIC_ADD = 0xA0 | ((address >> 15) & 0x0E);

    if (!EEPROM_WaitReady(IIC_ADD, 10))
      return false;

    I2C_Start();
    if (I2C_Write(IIC_ADD) != 0) {
      I2C_Stop();
      return false;
    }
    if (I2C_Write((address >> 8) & 0xFF) != 0) {
      I2C_Stop();
      return false;
    }
    if (I2C_Write(address & 0xFF) != 0) {
      I2C_Stop();
      return false;
    }

    // Пишем всю страницу за раз
    if (I2C_WriteBuffer(pBuffer, chunk_size) != 0) {
      I2C_Stop();
      return false;
    }

    I2C_Stop();

    // Ждем завершения записи
    if (!EEPROM_WaitReady(IIC_ADD, 10))
      return false;

    pBuffer += chunk_size;
    address += chunk_size;
    size -= chunk_size;
    gEepromWrite = true;
  }
  return true;
}

void EEPROM_WriteBuffer(uint32_t address, uint8_t *pBuffer, uint16_t size) {
  (void)EEPROM_TryWriteBuffer(address, pBuffer, size);
}

/* void EEPROM_WriteBuffer(uint32_t address, uint8_t *pBuffer, uint16_t size) {
  if (pBuffer == NULL || size == 0)
    return;

  uint16_t PAGE_SIZE = EEPROM_GetPageSize();
  address &= 0x3FFFF;

  while (size > 0) {
    uint16_t page_offset = address % PAGE_SIZE;
    uint16_t chunk_size =
        (size < (PAGE_SIZE - page_offset)) ? size : (PAGE_SIZE - page_offset);
    uint8_t IIC_ADD = 0xA0 | ((address >> 15) & 0x0E);

    if (!EEPROM_WaitReady(IIC_ADD, 10))
      return;
    TIMER_DelayUs(100);

    I2C_Start();
    if (I2C_Write(IIC_ADD) != 0) {
      I2C_Stop();
      return;
    }
    if (I2C_Write((address >> 8) & 0xFF) != 0) {
      I2C_Stop();
      return;
    }
    if (I2C_Write(address & 0xFF) != 0) {
      I2C_Stop();
      return;
    }

    for (uint16_t i = 0; i < chunk_size; i++) {
      if (I2C_Write(pBuffer[i]) != 0) {
        I2C_Stop();
        return;
      }
    }

    I2C_Stop();
    SYS_DelayMs(15);

    if (!EEPROM_WaitReady(IIC_ADD, 50))
      return;

    pBuffer += chunk_size;
    address += chunk_size;
    size -= chunk_size;
    gEepromWrite = true;
  }
} */

EEPROMType EEPROM_DetectType(void) {
  static uint8_t found_devices = 0;
  if (!found_devices) {
    for (uint8_t i = 0; i < 8; i++) {
      if (EEPROM_Detect(0xA0 | (i << 1)))
        found_devices++;
    }
  }

  switch (found_devices) {
  case 1:
    return EEPROM_BL24C64;
  case 2:
    return EEPROM_BL24C128;
  case 4:
    return EEPROM_M24M02;
  case 8:
    return EEPROM_4MBIT;
  default:
    return EEPROM_UNKNOWN;
  }
}

uint32_t EEPROM_DetectSize(void) {
  EEPROMType type = EEPROM_DetectType();
  return type < EEPROM_UNKNOWN ? EEPROM_SIZES[type] : 0;
}

uint16_t EEPROM_GetPageSize(void) {
  EEPROMType type = EEPROM_DetectType();
  return type < EEPROM_UNKNOWN ? PAGE_SIZES[type] : 0;
}

int EEPROM_ReadByte(uint32_t address, uint8_t *value) {
  return EEPROM_ReadBuffer(address, value, 1);
}

int EEPROM_WriteByte(uint32_t address, uint8_t value) {
  EEPROM_WriteBuffer(address, &value, 1);
  return 0;
}

bool EEPROM_Detect(uint8_t device_addr) {
  I2C_Start();
  int result = I2C_Write(device_addr);
  I2C_Stop();
  return (result == 0);
}

bool EEPROM_Test(uint32_t test_addr) {
  uint8_t test_data[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
  uint8_t read_data[8] = {0};

  EEPROM_WriteBuffer(test_addr, test_data, sizeof(test_data));
  if (EEPROM_ReadBuffer(test_addr, read_data, sizeof(read_data)) != 0)
    return false;

  return (memcmp(test_data, read_data, sizeof(test_data)) == 0);
}

#include "hal/uart.h"
void EEPROM_TestReadSpeed() {
  uint8_t buf[256];
  Log("-- EEPROM TEST START --");
  for (uint32_t i = 0; i < g_eeprom_size; i += g_eeprom_page_size) {
    EEPROM_ReadBuffer(i, buf, g_eeprom_page_size);
  }
  Log("-- EEPROM TEST END --");
}

void EEPROM_ScanBus(void) {
  printf("\nI2C Scan:\n     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");
  for (uint8_t row = 0; row < 8; row++) {
    printf("%02X: ", row * 16);
    for (uint8_t col = 0; col < 16; col++) {
      uint8_t addr = (row * 16 + col) << 1;
      if (addr < 0x10 || addr > 0xF0) {
        printf("   ");
        continue;
      }

      I2C_Start();
      int result = I2C_Write(addr);
      I2C_Stop();

      printf(result == 0 ? "%02X " : "-- ", addr);
      TIMER_DelayUs(100);
    }
    printf("\n");
  }
}
