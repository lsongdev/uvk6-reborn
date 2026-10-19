#ifndef UVK1_STORAGE_LAYOUT_H
#define UVK1_STORAGE_LAYOUT_H
// Where the UV-K1 SPI NOR flash keeps each storage area. Every area owns
// whole 4 KiB erase sectors, so erasing one never touches another.
#include "store/calibration.h"
#include "store/config-store.h"
#include "store/channel-store.h"
#include "drivers/py25q16/py25q16.h"

#define FLASH_BYTES (2u * 1024u * 1024u)
// Stock firmware and the reference keep data below 0x020000 (channels and
// settings, calibration at 0x010000, boot logo, an RX/TX log); 0x012000 to
// at least 0x0ACFFF read back erased on this radio.
#define CAL_PHYSICAL 0x010000u

// Each configuration copy is a log of positions: payload, then an 8-byte
// header (counter, ~counter) programmed last. The newest valid header wins.
#define CONFIG_LOG_SECTORS 16u
#define CONFIG_LOG_STRIDE 2048u
#define CONFIG_LOG_HEADER (CONFIG_STORE_SLOT_SIZE)
#define CONFIG_LOG_POSITIONS (CONFIG_LOG_SECTORS * PY25Q16_SECTOR_BYTES / CONFIG_LOG_STRIDE)
#define CONFIG_LOG_BYTES (CONFIG_LOG_SECTORS * PY25Q16_SECTOR_BYTES)
#define CONFIG_A_PHYSICAL 0x020000u
#define CONFIG_B_PHYSICAL (CONFIG_A_PHYSICAL + CONFIG_LOG_BYTES)

// Channel copies are plain arrays, rewritten one whole sector at a time.
#define CHANNEL_SECTORS \
  ((CHANNEL_STORE_AREA_BYTES + PY25Q16_SECTOR_BYTES - 1u) / PY25Q16_SECTOR_BYTES)
#define CHANNEL_AREA_BYTES (CHANNEL_SECTORS * PY25Q16_SECTOR_BYTES)
#define CHANNELS_A_PHYSICAL (CONFIG_B_PHYSICAL + CONFIG_LOG_BYTES)
#define CHANNELS_B_PHYSICAL (CHANNELS_A_PHYSICAL + CHANNEL_AREA_BYTES)

// A channel sector is rewritten through this journal: the new content and
// then a header naming its target; boot replays a header left behind.
#define JOURNAL_DATA_PHYSICAL (CHANNELS_B_PHYSICAL + CHANNEL_AREA_BYTES)
#define JOURNAL_HEAD_PHYSICAL (JOURNAL_DATA_PHYSICAL + PY25Q16_SECTOR_BYTES)

// The only physical range the firmware may ever erase or program.
#define WRITABLE_START CONFIG_A_PHYSICAL
#define WRITABLE_END (JOURNAL_HEAD_PHYSICAL + PY25Q16_SECTOR_BYTES)

_Static_assert(CONFIG_LOG_HEADER + 8u <= CONFIG_LOG_STRIDE, "Header fits its position");
_Static_assert(PY25Q16_SECTOR_BYTES % CONFIG_LOG_STRIDE == 0, "Positions tile sectors");
_Static_assert(CONFIG_LOG_SECTORS >= 2, "Never erase the current position's sector");
_Static_assert(PY25Q16_SECTOR_BYTES % CHANNEL_STORE_RECORD_BYTES == 0,
               "A channel record never spans two sectors");
_Static_assert(WRITABLE_START >= 0x020000u, "Clear of stock data and the RX/TX log");
_Static_assert(WRITABLE_START >= CAL_PHYSICAL + PY25Q16_SECTOR_BYTES,
               "Never erase the factory calibration sector");
_Static_assert(WRITABLE_END <= 0x0AD000u, "Only sectors read back erased");
#endif
