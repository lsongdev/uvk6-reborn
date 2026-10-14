#ifndef CORE_RESOURCE_H
#define CORE_RESOURCE_H
#include <stdbool.h>
#include <stdint.h>

// Resources the PC tool (tools/resources.py) installs at the top of the
// EEPROM, outside the configuration and channels. Each is a 16-byte header
// followed by its data:
//   magic[4] | length u32 LE | CRC-32 of the data u32 LE | 0xFF x4
// The tool writes the header last and clears it first when replacing, so an
// interrupted install never verifies.
#define RESOURCE_HEADER_BYTES 16u
#define RESOURCE_SSB_PATCH_BYTES 8840u // SI4735/32 SSB patch (patch_init).
#define RESOURCE_SSB_PATCH_MAGIC "SSBP"
// Bytes kept for resources at the top of the EEPROM; new resources go below.
#define RESOURCE_TAIL_BYTES (RESOURCE_HEADER_BYTES + RESOURCE_SSB_PATCH_BYTES)

// Reads size bytes at address; false on a bus error.
typedef bool (*ResourceRead)(uint32_t address, uint8_t *out, uint16_t size);

// Where the SSB patch data starts (its header is just below), 0 when the
// EEPROM is too small to hold resources.
uint32_t RESOURCE_SsbPatchAddress(uint32_t capacity);
// The header before data_address names magic and length, and the data
// matches its CRC.
bool RESOURCE_Valid(ResourceRead read, uint32_t data_address,
                    const char magic[4], uint32_t length);
#endif
