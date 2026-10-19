#include "store/resource.h"
#include "core/crc32.h"
#include "hal/storage.h"
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

static bool readArea(uint32_t offset, uint8_t *out, uint16_t size) {
  return STORAGE_Read(STORAGE_RESOURCES, offset, out, size);
}

// Installed by tools/resources.py; its header carries the length and CRC.
bool RESOURCE_SsbPatchPresent(void) {
  return STORAGE_Size(STORAGE_RESOURCES) >= RESOURCE_TAIL_BYTES &&
         RESOURCE_Valid(readArea, RESOURCE_SSB_PATCH_OFFSET, RESOURCE_SSB_PATCH_MAGIC,
                        RESOURCE_SSB_PATCH_BYTES);
}

bool RESOURCE_SsbPatchRead(uint16_t offset, uint8_t *out, uint16_t size) {
  return offset < RESOURCE_SSB_PATCH_BYTES && size <= RESOURCE_SSB_PATCH_BYTES - offset &&
         readArea(RESOURCE_SSB_PATCH_OFFSET + offset, out, size);
}
