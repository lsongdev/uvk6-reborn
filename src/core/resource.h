#ifndef CORE_RESOURCE_H
#define CORE_RESOURCE_H
#include <stdbool.h>
#include <stdint.h>

// Resources the PC tool (tools/resources.py) installs in STORAGE_RESOURCES,
// outside the configuration and channels. Each is a 16-byte header followed
// by its data:
//   magic[4] | length u32 LE | CRC-32 of the data u32 LE | 0xFF x4
// The tool writes the header last and clears it first when replacing, so an
// interrupted install never verifies.
#define RESOURCE_HEADER_BYTES 16u
#define RESOURCE_SSB_PATCH_BYTES 8840u // SI4735/32 SSB patch (patch_init).
#define RESOURCE_SSB_PATCH_MAGIC "SSBP"
// Size of the resources area; new resources go below the SSB patch.
#define RESOURCE_TAIL_BYTES (RESOURCE_HEADER_BYTES + RESOURCE_SSB_PATCH_BYTES)
// Where the SSB patch data starts in the area; its header is just below.
#define RESOURCE_SSB_PATCH_OFFSET RESOURCE_HEADER_BYTES

// Reads size bytes at an area offset; false on a bus error.
typedef bool (*ResourceRead)(uint32_t offset, uint8_t *out, uint16_t size);

// The header before data_offset names magic and length, and the data
// matches its CRC.
bool RESOURCE_Valid(ResourceRead read, uint32_t data_offset,
                    const char magic[4], uint32_t length);
#endif
