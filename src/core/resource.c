#include "core/resource.h"
#include "core/crc32.h"
#include <string.h>

static uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t RESOURCE_SsbPatchAddress(uint32_t capacity) {
  // Small parts hold the configuration and channels, never resources.
  return capacity >= 0x20000u ? capacity - RESOURCE_SSB_PATCH_BYTES : 0;
}

bool RESOURCE_Valid(ResourceRead read, uint32_t data_address,
                    const char magic[4], uint32_t length) {
  uint8_t buf[32];
  if (data_address < RESOURCE_HEADER_BYTES ||
      !read(data_address - RESOURCE_HEADER_BYTES, buf, RESOURCE_HEADER_BYTES) ||
      memcmp(buf, magic, 4) != 0 || get32(buf + 4) != length)
    return false;
  uint32_t expected = get32(buf + 8), crc = CRC32_INIT;
  for (uint32_t offset = 0; offset < length; offset += sizeof(buf)) {
    uint16_t count = length - offset > sizeof(buf) ? sizeof(buf) : (uint16_t)(length - offset);
    if (!read(data_address + offset, buf, count)) return false;
    crc = CRC32_Update(crc, buf, count);
  }
  return ~crc == expected;
}
