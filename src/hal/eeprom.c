#include "hal/eeprom.h"
#include "hal/eeprom-layout.h"
#include "hal/scheduler.h"
#include "hal/i2c.h"
#include "hal/system.h"
#include "hal/systick.h"
#include <string.h>

bool gEepromWrite = false;
static bool writes_enabled = false;

void EEPROM_SetWritesEnabled(bool enabled) { writes_enabled = enabled; }

static uint32_t g_eeprom_size = 0;

void EEPROM_Init(void) {
  g_eeprom_size = EEPROM_DetectSize();
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

// Sequential reads split at 64-KiB bank boundaries.
// Keep the transaction within a single bank.
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



// Page-split write with no layout policy; every caller must apply one.
static bool writeUnchecked(uint32_t address, const uint8_t *pBuffer, uint16_t size) {
  if (pBuffer == NULL || address >= g_eeprom_size || size > g_eeprom_size - address)
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

bool EEPROM_TryWriteBuffer(uint32_t address, const uint8_t *pBuffer, uint16_t size) {
  return writes_enabled && EEPROM_WriteAllowed(address, size) &&
         writeUnchecked(address, pBuffer, size);
}

bool EEPROM_WriteResource(uint32_t address, const uint8_t *pBuffer, uint16_t size) {
  uint8_t back[32];
  if (!writes_enabled || !EEPROM_ResourceWriteAllowed(address, size, EEPROM_DetectSize()) ||
      !writeUnchecked(address, pBuffer, size))
    return false;
  for (uint16_t at = 0; at < size; at += sizeof(back)) { // Read back.
    uint16_t n = (uint16_t)(size - at);
    if (n > sizeof(back)) n = sizeof(back);
    if (EEPROM_ReadBuffer(address + at, back, n) != 0 || memcmp(back, pBuffer + at, n) != 0)
      return false;
  }
  return true;
}

#if UVK6_SEED_PA_CAL
// One-off maintenance build for a radio whose factory calibration was erased.
// Seeds only empty VHF/UHF PA rows (0x1ED0 + band*16, Low/Mid/High x 3
// anchors) and verifies by read-back; existing calibration is never touched.
// Low/Mid are the identical factory values in three public UV-K5(8) dumps;
// High is the per-anchor minimum of those dumps (conservative, unmeasured).
static bool seedRow(uint32_t address, const uint8_t row[9]) {
  uint8_t current[9];
  if (EEPROM_ReadBuffer(address, current, sizeof(current)) != 0) return false;
  bool empty00 = true, emptyFF = true;
  for (uint8_t i = 0; i < sizeof(current); ++i) {
    empty00 &= current[i] == 0x00;
    emptyFF &= current[i] == 0xFF;
  }
  if (!empty00 && !emptyFF) return memcmp(current, row, sizeof(current)) == 0;
  return writeUnchecked(address, row, 9) &&
         EEPROM_ReadBuffer(address, current, sizeof(current)) == 0 &&
         memcmp(current, row, sizeof(current)) == 0;
}
bool EEPROM_SeedPaCalibration(void) {
  static const uint8_t vhf[9] = {0x5F,0x5F,0x5F, 0x69,0x69,0x69, 0x7A,0x8B,0x7F};
  static const uint8_t uhf[9] = {0x5A,0x5A,0x5A, 0x64,0x64,0x64, 0x7B,0x7B,0x7A};
  bool ok = seedRow(0x1ED0u + 2u*16u, vhf);
  return seedRow(0x1ED0u + 5u*16u, uhf) && ok;
}
#endif




EEPROMType EEPROM_DetectType(void) {
  static bool probed;
  static EEPROMType candidate = EEPROM_UNKNOWN;
  if (!probed) {
    uint8_t ackMask = 0;
    for (uint8_t i = 0; i < 8; ++i)
      if (EEPROM_Detect(0xA0 | (i << 1)))
        ackMask |= (uint8_t)(1u << i);
    // Reject scattered/partial sets instead of guessing from ACK count.
    candidate = EEPROM_CandidateFromAckMask(ackMask);
    probed = true;
  }
  return candidate; // Candidate mapping, not verified chip identification.
}

uint32_t EEPROM_DetectSize(void) {
  EEPROMType type = EEPROM_DetectType();
  return type < EEPROM_UNKNOWN ? EEPROM_SIZES[type] : 0;
}

uint16_t EEPROM_GetPageSize(void) {
  EEPROMType type = EEPROM_DetectType();
  return type < EEPROM_UNKNOWN ? PAGE_SIZES[type] : 0;
}



bool EEPROM_Detect(uint8_t device_addr) {
  I2C_Start();
  int result = I2C_Write(device_addr);
  I2C_Stop();
  return (result == 0);
}

#include "hal/uart.h"

