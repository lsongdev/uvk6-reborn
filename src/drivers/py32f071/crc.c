#include "hal/crc.h"

// CRC-16/XMODEM of the PC link, in software: the PY32F071 CRC unit is CRC-32.
void CRC_Init(void) {}

uint16_t CRC_Calculate(const void *pBuffer, uint16_t Size) {
  const uint8_t *p = pBuffer;
  uint16_t crc = 0;
  while (Size--) {
    crc ^= (uint16_t)(*p++ << 8);
    for (uint8_t i = 0; i < 8; ++i)
      crc = crc & 0x8000u ? (uint16_t)(crc << 1 ^ 0x1021u) : (uint16_t)(crc << 1);
  }
  return crc;
}
