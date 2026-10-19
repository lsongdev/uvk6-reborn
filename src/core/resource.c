#include "core/resource.h"
#include "core/crc32.h"
#include <string.h>

static uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool RESOURCE_Valid(ResourceRead read, uint32_t data_offset,
                    const char magic[4], uint32_t length) {
  uint8_t buf[32];
  if (data_offset < RESOURCE_HEADER_BYTES ||
      !read(data_offset - RESOURCE_HEADER_BYTES, buf, RESOURCE_HEADER_BYTES) ||
      memcmp(buf, magic, 4) != 0 || get32(buf + 4) != length)
    return false;
  uint32_t expected = get32(buf + 8), crc = CRC32_INIT;
  for (uint32_t offset = 0; offset < length; offset += sizeof(buf)) {
    uint16_t count = length - offset > sizeof(buf) ? sizeof(buf) : (uint16_t)(length - offset);
    if (!read(data_offset + offset, buf, count)) return false;
    crc = CRC32_Update(crc, buf, count);
  }
  return ~crc == expected;
}
