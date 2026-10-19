#include "hal/storage.h"
#include "hal/board.h"
#include "boards/uvk1/pins.h"
#include "boards/uvk1/storage-layout.h"
#include "drivers/py32f071/spi.h"
#include "core/crc32.h"
#include <string.h>

static uint32_t capacity;
static uint32_t jedec;
static bool writable, writesEnabled;
static char name[] = "SPI 000000 RO";
bool gStorageWrite;

static void selectFlash(bool selected) {
  // Transfer waits for RXNE; wait for the last clock before deasserting CS.
  while (SPI2->SR & SPI_SR_BSY) continue;
  GPIOA->BSRR = 1u << (UVK1_FLASH_CS + (selected ? 16u : 0u));
}
static const Py25q16Bus bus = {selectFlash, PY32_SPI2_Transfer};

// Newest valid position of each configuration log, or -1 when it has none.
typedef struct { int8_t current; uint32_t counter; } ConfigLog;
static ConfigLog logs[2];
// A channel write that must clear and set bits rewrites its whole sector.
static uint8_t sector[PY25Q16_SECTOR_BYTES];

#define POSITIONS_PER_SECTOR (PY25Q16_SECTOR_BYTES / CONFIG_LOG_STRIDE)

static uint32_t get32(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t v) {
  p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}
static uint32_t position(unsigned copy, unsigned p) {
  return (copy ? CONFIG_B_PHYSICAL : CONFIG_A_PHYSICAL) + p * CONFIG_LOG_STRIDE;
}
static bool header(uint32_t at, uint32_t *counter) {
  uint8_t h[8];
  PY25Q16_Read(&bus, at + CONFIG_LOG_HEADER, h, sizeof(h));
  uint32_t c = get32(h);
  if (c == 0xFFFFFFFFu || (c ^ get32(h + 4)) != 0xFFFFFFFFu) return false;
  *counter = c;
  return true;
}
static void scanLog(unsigned copy) {
  ConfigLog log = {-1, 0};
  for (unsigned p = 0; p < CONFIG_LOG_POSITIONS; ++p) {
    uint32_t c;
    if (header(position(copy, p), &c) && (log.current < 0 || c > log.counter))
      log = (ConfigLog){(int8_t)p, c};
  }
  logs[copy] = log;
}
static bool blank(uint32_t at, uint32_t size) {
  uint8_t chunk[64];
  for (uint32_t done = 0; done < size; done += sizeof(chunk)) {
    uint16_t n = size - done < sizeof(chunk) ? (uint16_t)(size - done) : sizeof(chunk);
    PY25Q16_Read(&bus, at + done, chunk, n);
    for (uint16_t i = 0; i < n; ++i)
      if (chunk[i] != 0xFF) return false;
  }
  return true;
}

// Every erase and program passes here: nothing outside our own sectors,
// in particular never the factory calibration, can be changed.
static bool allowed(uint32_t at, uint32_t size) {
  return writable && writesEnabled && size && at >= WRITABLE_START &&
         at < WRITABLE_END && size <= WRITABLE_END - at;
}
static bool erase(uint32_t at) {
  return allowed(at, PY25Q16_SECTOR_BYTES) && PY25Q16_EraseSector(&bus, at);
}
static bool program(uint32_t at, const void *data, uint16_t size) {
  return allowed(at, size) && PY25Q16_Program(&bus, at, data, size);
}
static bool eraseIfUsed(uint32_t at) {
  return blank(at, PY25Q16_SECTOR_BYTES) || erase(at);
}
// Erases the target, then programs the buffer's non-erased pages.
static bool rewrite(uint32_t start) {
  if (!erase(start)) return false;
  for (uint32_t page = 0; page < PY25Q16_SECTOR_BYTES; page += PY25Q16_PAGE_BYTES) {
    bool erased = true;
    for (uint32_t i = 0; i < PY25Q16_PAGE_BYTES && erased; ++i) erased = sector[page + i] == 0xFF;
    if (!erased && !program(start + page, sector + page, PY25Q16_PAGE_BYTES)) return false;
  }
  return true;
}

// Journal header: magic, target sector, CRC32 of the journalled content.
#define JOURNAL_MAGIC 0x4E4A314Bu // "K1JN"
static uint32_t sectorCrc(void) {
  return ~CRC32_Update(CRC32_INIT, sector, PY25Q16_SECTOR_BYTES);
}
// The buffer's content reaches its target sector whole, or not at all: a
// copy and its header go to the journal first, and boot finishes the job.
static bool commitSector(uint32_t start) {
  uint8_t h[12];
  put32(h, JOURNAL_MAGIC);
  put32(h + 4, start);
  put32(h + 8, sectorCrc());
  return eraseIfUsed(JOURNAL_HEAD_PHYSICAL) && eraseIfUsed(JOURNAL_DATA_PHYSICAL) &&
         program(JOURNAL_DATA_PHYSICAL, sector, PY25Q16_SECTOR_BYTES) &&
         program(JOURNAL_HEAD_PHYSICAL, h, sizeof(h)) &&
         rewrite(start) && erase(JOURNAL_HEAD_PHYSICAL);
}
// A header still present means its rewrite may not have finished.
static void replayJournal(void) {
  uint8_t h[12];
  PY25Q16_Read(&bus, JOURNAL_HEAD_PHYSICAL, h, sizeof(h));
  uint32_t start = get32(h + 4);
  if (get32(h) != JOURNAL_MAGIC || start % PY25Q16_SECTOR_BYTES ||
      start < CHANNELS_A_PHYSICAL || start >= JOURNAL_DATA_PHYSICAL) return;
  PY25Q16_Read(&bus, JOURNAL_DATA_PHYSICAL, sector, PY25Q16_SECTOR_BYTES);
  if (sectorCrc() != get32(h + 8)) return; // Header without its data.
  bool enabled = writesEnabled;
  writesEnabled = true; // Finishing a write that was already allowed.
  if (rewrite(start)) (void)erase(JOURNAL_HEAD_PHYSICAL);
  writesEnabled = enabled;
}

void STORAGE_Init(void) {
  capacity = 0;
  writable = false;
  name[10] = ' '; // " RO" until the flash proves writable.
  PY32_SPI2_Init();
  jedec = PY25Q16_ReadJedecId(&bus);
  for (unsigned i = 0; i < 6; ++i)
    name[4 + i] = "0123456789ABCDEF"[(jedec >> (20u - 4u * i)) & 15u];
  // 2 MiB NOR: EB/60/15 measured on this K1; also support PY25Q16.
  // Fail closed on other IDs.
  if (jedec != 0xEB6015u && jedec != 0x856015u) return;
  capacity = FLASH_BYTES;
  // Block protection would silently drop writes; never rewrite the status
  // register (it also holds configuration bits), just stay read-only.
  if (PY25Q16_ReadStatus(&bus) & PY25Q16_STATUS_PROTECT) return;
  writable = true;
  name[10] = '\0'; // Drop the " RO" suffix.
  scanLog(0);
  scanLog(1);
  replayJournal();
}
bool STORAGE_Ready(void) { return capacity != 0; }
const char *STORAGE_Name(void) { return name; }
uint32_t STORAGE_Capacity(void) { return capacity; }
uint32_t STORAGE_Size(StorageArea area) {
  if (!capacity) return 0;
  switch (area) {
  case STORAGE_CALIBRATION: return CALIBRATION_BYTES;
  case STORAGE_CONFIG_A: case STORAGE_CONFIG_B:
    return writable ? CONFIG_STORE_SLOT_SIZE : 0;
  case STORAGE_CHANNELS_A: case STORAGE_CHANNELS_B:
    return writable ? CHANNEL_STORE_AREA_BYTES : 0;
  default: return 0;
  }
}
void STORAGE_SetWritesEnabled(bool enabled) { writesEnabled = enabled; }

static uint32_t channelBase(StorageArea area) {
  return area == STORAGE_CHANNELS_B ? CHANNELS_B_PHYSICAL : CHANNELS_A_PHYSICAL;
}
bool STORAGE_Read(StorageArea area, uint32_t offset, void *data, uint16_t size) {
  uint32_t limit = STORAGE_Size(area);
  if (!data || !size || offset >= limit || size > limit - offset) return false;
  switch (area) {
  case STORAGE_CALIBRATION:
    return BOARD_RawRead(CAL_PHYSICAL + offset, data, size);
  case STORAGE_CONFIG_A: case STORAGE_CONFIG_B: {
    const ConfigLog *log = &logs[area == STORAGE_CONFIG_B];
    if (log->current < 0) memset(data, 0xFF, size); // Nothing written yet.
    else PY25Q16_Read(&bus, position(area == STORAGE_CONFIG_B, log->current) + offset, data, size);
    return true;
  }
  default:
    PY25Q16_Read(&bus, channelBase(area) + offset, data, size);
    return true;
  }
}

// Appends a whole record at the next position. The sector being entered is
// erased first; it never holds the current position, so an interrupted write
// leaves the previous record readable. A position spoiled by an earlier
// interruption is skipped by moving on to the next sector.
static bool writeConfig(unsigned copy, const void *data, uint16_t size) {
  if (size != CONFIG_STORE_SLOT_SIZE) return false;
  ConfigLog *log = &logs[copy];
  unsigned next = log->current < 0 ? 0 : (log->current + 1u) % CONFIG_LOG_POSITIONS;
  if (next % POSITIONS_PER_SECTOR && !blank(position(copy, next), CONFIG_LOG_HEADER + 8u))
    next = (next / POSITIONS_PER_SECTOR + 1u) * POSITIONS_PER_SECTOR % CONFIG_LOG_POSITIONS;
  uint32_t at = position(copy, next);
  if (!(next % POSITIONS_PER_SECTOR) && !blank(at, PY25Q16_SECTOR_BYTES) && !erase(at))
    return false;
  uint32_t counter = log->current < 0 ? 1u : log->counter + 1u, check;
  uint8_t h[8];
  put32(h, counter);
  put32(h + 4, ~counter);
  if (!program(at, data, size) || !program(at + CONFIG_LOG_HEADER, h, sizeof(h)) ||
      !header(at, &check) || check != counter)
    return false;
  *log = (ConfigLog){(int8_t)next, counter};
  return true;
}

// Programs in place when only bits need clearing (a delete, or a record in
// erased space); otherwise rewrites the record's sector through the journal.
static bool writeChannels(uint32_t at, const uint8_t *data, uint16_t size) {
  uint32_t start = at - at % PY25Q16_SECTOR_BYTES;
  if (size > start + PY25Q16_SECTOR_BYTES - at) return false;
  PY25Q16_Read(&bus, at, sector, size);
  bool same = true, clearOnly = true;
  for (uint16_t i = 0; i < size; ++i) {
    same &= sector[i] == data[i];
    clearOnly &= (sector[i] & data[i]) == data[i];
  }
  if (same) return true;
  if (clearOnly) return program(at, data, size);
  PY25Q16_Read(&bus, start, sector, PY25Q16_SECTOR_BYTES);
  memcpy(sector + (at - start), data, size);
  return commitSector(start);
}

bool STORAGE_Write(StorageArea area, uint32_t offset, const void *data, uint16_t size) {
  uint32_t limit = STORAGE_Size(area);
  if (!writable || !writesEnabled || !data || !size || offset >= limit || size > limit - offset)
    return false;
  bool ok;
  switch (area) {
  case STORAGE_CONFIG_A: case STORAGE_CONFIG_B:
    ok = !offset && writeConfig(area == STORAGE_CONFIG_B, data, size);
    break;
  case STORAGE_CHANNELS_A: case STORAGE_CHANNELS_B:
    ok = writeChannels(channelBase(area) + offset, data, size);
    break;
  default:
    return false; // Calibration and resources are never written here.
  }
  if (ok) gStorageWrite = true;
  return ok;
}

uint32_t BOARD_RawSize(void) { return capacity; }
bool BOARD_RawRead(uint32_t address, void *data, uint16_t size) {
  if (!data || !size || address >= capacity || size > capacity - address) return false;
  PY25Q16_Read(&bus, address, data, size);
  return true;
}
// The PC tool may not write the K1 flash: its calibration sector has no
// copy elsewhere, and resources are not used on this board.
bool BOARD_MaintenanceWrite(uint32_t address, const uint8_t *data, uint16_t size) {
  (void)address; (void)data; (void)size;
  return false;
}
