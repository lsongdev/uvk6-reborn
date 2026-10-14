#include "core/crc32.h"

uint32_t CRC32_Update(uint32_t crc, const uint8_t *bytes, uint32_t size) {
  while (size--) {
    crc ^= *bytes++;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return crc;
}
