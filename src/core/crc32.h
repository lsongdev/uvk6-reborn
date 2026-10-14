#ifndef CORE_CRC32_H
#define CORE_CRC32_H
#include <stdint.h>
// CRC-32 (IEEE 802.3, as zlib) in pieces: start from CRC32_INIT, feed bytes
// with CRC32_Update, and the result is the complement (~crc).
#define CRC32_INIT 0xffffffffu
uint32_t CRC32_Update(uint32_t crc, const uint8_t *bytes, uint32_t size);
#endif
