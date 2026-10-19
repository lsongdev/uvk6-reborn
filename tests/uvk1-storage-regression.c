// The real K1 board storage, NOR driver, configuration and channel stores
// over an SPI-level NOR model: programming only clears bits, a page program
// wraps within its page, and power can fail in the middle of any erase or
// program. Nothing outside our sectors, the calibration above all, changes.
#define UVK1_PINS_H
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define UVK1_FLASH_CS 3u
#define SPI_SR_BSY 0x80u
static struct { uint32_t SR; } spi;
static struct { uint32_t BSRR; } gpio;
static void edges(void);
// The board waits on SR before every CS change: see each edge there.
#define SPI2 (edges(), &spi)
#define GPIOA (&gpio)
#include "../src/boards/uvk1/storage.c"
#include "store/config-store.h"
#include "store/channel-store.h"

static uint8_t flash[FLASH_BYTES], before[FLASH_BYTES];
static uint32_t jedecReply = 0xEB6015u;
static uint8_t statusBits; // Block protection as found on the chip.
static bool selected, wel, dead;
static uint8_t opcode, page[PY25Q16_PAGE_BYTES];
static uint32_t index_, address, busy, delays;
static uint32_t mutations, cutAt = UINT32_MAX, erases[FLASH_BYTES / PY25Q16_SECTOR_BYTES];
static uint16_t programmed;

static void begin(void) { index_ = 0; programmed = 0; }
static void end(void) {
  if (index_ == 1 && opcode == 0x06) wel = !dead;
  if (!wel || index_ < 4 || (opcode != 0x20 && opcode != 0x02)) return;
  wel = false;
  bool cut = mutations++ == cutAt;
  if (opcode == 0x20) {
    uint32_t start = address - address % PY25Q16_SECTOR_BYTES;
    ++erases[start / PY25Q16_SECTOR_BYTES];
    memset(flash + start, 0xFF, cut ? PY25Q16_SECTOR_BYTES / 2 : PY25Q16_SECTOR_BYTES);
    busy = 3;
  } else {
    uint16_t n = cut ? programmed / 2 : programmed;
    uint32_t base = address - address % PY25Q16_PAGE_BYTES;
    for (uint16_t i = 0; i < n; ++i) // Wraps inside the page, like the chip.
      flash[base + (address + i) % PY25Q16_PAGE_BYTES] &= page[i];
    busy = 1;
  }
  if (cut) dead = true; // Power is gone: nothing more reaches the flash.
}
static void edges(void) {
  bool low = gpio.BSRR == 1u << (UVK1_FLASH_CS + 16u);
  if (low == selected) return;
  if (selected) end(); else begin();
  selected = low;
}
void PY32_SPI2_Init(void) {}
uint8_t PY32_SPI2_Transfer(uint8_t value) {
  edges();
  assert(selected);
  uint32_t i = index_++;
  if (!i) { opcode = value; return 0xFF; }
  switch (opcode) {
  case 0x9F: return i <= 3 ? (uint8_t)(jedecReply >> (8 * (3 - i))) : 0xFF;
  case 0x05:
    if (busy) { --busy; return statusBits | PY25Q16_STATUS_BUSY | (wel ? 2 : 0); }
    return statusBits | (wel ? PY25Q16_STATUS_WEL : 0);
  case 0x03: case 0x02: case 0x20:
    if (i <= 3) { address = (address << 8 | value) & 0xFFFFFFu; return 0xFF; }
    if (opcode == 0x03) return flash[address + i - 4 < FLASH_BYTES ? address + i - 4 : 0];
    if (opcode == 0x02 && programmed < sizeof(page)) page[programmed++] = value;
    return 0xFF;
  default: return 0xFF;
  }
}
void SYS_DelayMs(uint32_t ms) { delays += ms; }
static void flush(void) { edges(); }

static void boot(void) {
  flush();
  dead = false;
  STORAGE_Init();
  flush();
  STORAGE_SetWritesEnabled(true);
}
// Every byte outside our own sectors is exactly as it was.
static void untouchedOutside(void) {
  flush();
  assert(!memcmp(flash, before, WRITABLE_START));
  assert(!memcmp(flash + WRITABLE_END, before + WRITABLE_END, FLASH_BYTES - WRITABLE_END));
}

static bool readSlot(void *c, unsigned slot, uint16_t offset, void *out, uint16_t size) {
  (void)c; return STORAGE_Read(slot ? STORAGE_CONFIG_B : STORAGE_CONFIG_A, offset, out, size);
}
static bool writeSlot(void *c, unsigned slot, const void *data, uint16_t size) {
  (void)c; return STORAGE_Write(slot ? STORAGE_CONFIG_B : STORAGE_CONFIG_A, 0, data, size);
}
static const ConfigStore store = {NULL, readSlot, writeSlot};
static DeviceConfig configFor(uint32_t n) {
  DeviceConfig c; CONFIG_Default(&c);
  c.preferences.mic = (uint8_t)(n % 32u);
  c.preferences.tx_timeout_seconds = (uint16_t)(30u + n % 200u);
  return c;
}
static bool loadedIs(uint32_t n) {
  DeviceConfig c; uint32_t sequence;
  return CONFIG_Load(&store, &c, &sequence) && c.preferences.mic == n % 32u &&
         c.preferences.tx_timeout_seconds == 30u + n % 200u;
}
static UserChannel channel(uint32_t hz) {
  return (UserChannel){.name = "K1", .frequency_hz = hz,
    .rx = {.mode = RX_NFM, .step_hz = 12500, .bandwidth_hz = 12000, .squelch = 4}};
}
static bool channelIs(uint16_t number, uint32_t hz) {
  UserChannel c;
  return CHANNEL_STORE_Load(number, &c) && c.frequency_hz == hz;
}

static void testDriver(void) {
  // A program crossing a page boundary is split, never wrapped.
  memset(flash, 0xFF, sizeof(flash)); boot();
  uint8_t data[300];
  for (unsigned i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)i;
  assert(program(WRITABLE_START + 200, data, sizeof(data)));
  flush();
  assert(!memcmp(flash + WRITABLE_START + 200, data, sizeof(data)));
  assert(flash[WRITABLE_START] == 0xFF && flash[WRITABLE_START + 500] == 0xFF);
  assert(!PY25Q16_EraseSector(&bus, WRITABLE_START + 1)); // Unaligned erase refused.
  // A chip that refuses the write enable fails the write, without a hang.
  dead = true;
  assert(!PY25Q16_Program(&bus, WRITABLE_START + 4096, data, 1));
  dead = false;
}

static void testIdentity(void) {
  uint8_t data[8];
  memset(flash, 0xFF, sizeof(flash));
  jedecReply = 0x856015u; boot(); assert(STORAGE_Ready());
  jedecReply = 0xEB6015u; boot();
  assert(STORAGE_Ready() && BOARD_RawSize() == FLASH_BYTES && !strcmp(STORAGE_Name(), "SPI EB6015"));
  assert(STORAGE_Size(STORAGE_CONFIG_A) == CONFIG_STORE_SLOT_SIZE);
  assert(STORAGE_Size(STORAGE_CHANNELS_B) == CHANNEL_STORE_AREA_BYTES);
  assert(STORAGE_Size(STORAGE_CALIBRATION) == 512 && !STORAGE_Size(STORAGE_RESOURCES));
  flash[CAL_PHYSICAL + 0xC0] = 0x42;
  assert(STORAGE_Read(STORAGE_CALIBRATION, 0xC0, data, 1) && data[0] == 0x42);
  assert(!STORAGE_Read(STORAGE_CALIBRATION, 511, data, 2));
  assert(!BOARD_RawRead(0x200000, data, 1) && !BOARD_RawRead(0x1FFFFF, data, 2));
  assert(!BOARD_RawRead(0, data, 0) && !BOARD_RawRead(0, NULL, 1));
  assert(!STORAGE_Write(STORAGE_CALIBRATION, 0, data, 1));
  assert(!STORAGE_Write(STORAGE_RESOURCES, 0, data, 1));
  assert(!BOARD_MaintenanceWrite(CAL_PHYSICAL, data, 1));
  // Block-protected chip: read-only, no configuration area at all.
  statusBits = 0x1C; boot();
  assert(STORAGE_Ready() && !strcmp(STORAGE_Name(), "SPI EB6015 RO"));
  assert(!STORAGE_Size(STORAGE_CONFIG_A) && !STORAGE_Size(STORAGE_CHANNELS_A));
  assert(!STORAGE_Write(STORAGE_CONFIG_A, 0, data, 1));
  statusBits = 0;
  jedecReply = 0xFFFFFFu; boot(); assert(!STORAGE_Ready() && !BOARD_RawRead(0, data, 1));
  jedecReply = 0x856017u; boot(); assert(!STORAGE_Ready()); // Unknown capacity.
  jedecReply = 0xEB6015u;
}

static void testConfig(void) {
  // Stock-looking data everywhere else, and a recognisable calibration.
  for (uint32_t i = 0; i < FLASH_BYTES; ++i) flash[i] = (uint8_t)(i * 7u + 3u);
  memset(flash + WRITABLE_START, 0xFF, WRITABLE_END - WRITABLE_START);
  memcpy(before, flash, sizeof(flash));
  memset(erases, 0, sizeof(erases));
  mutations = 0;
  boot();
  DeviceConfig c;
  assert(!CONFIG_Load(&store, &c, NULL)); // Empty: no configuration yet.
  // Writes stay off until enabled; nothing reaches the flash.
  STORAGE_SetWritesEnabled(false);
  c = configFor(1);
  assert(!CONFIG_Save(&store, &c, NULL) && !mutations);
  STORAGE_SetWritesEnabled(true);
  // Many saves wrap both logs; each reboot finds the newest.
  enum { SAVES = 3 * CONFIG_LOG_POSITIONS * 2 + 5 };
  for (uint32_t n = 1; n <= SAVES; ++n) {
    c = configFor(n);
    assert(CONFIG_Save(&store, &c, NULL));
    if (n % 7 == 0 || n == SAVES) { boot(); assert(loadedIs(n)); }
  }
  // Wear: each log sector is erased about once per lap of its log.
  for (uint32_t s = WRITABLE_START / PY25Q16_SECTOR_BYTES; s < CHANNELS_A_PHYSICAL / PY25Q16_SECTOR_BYTES; ++s)
    assert(erases[s] <= SAVES / 2 / CONFIG_LOG_POSITIONS + 1);
  untouchedOutside();

  // Power fails at every erase and program step of a save: after reboot
  // the radio holds either the previous or the new configuration.
  uint32_t n = SAVES, cuts = 0;
  for (uint32_t step = 0; step < 64; ++step) {
    boot();
    assert(loadedIs(n));
    c = configFor(n + 1);
    mutations = 0; cutAt = step;
    bool saved = CONFIG_Save(&store, &c, NULL);
    cutAt = UINT32_MAX;
    cuts += dead;
    boot();
    if (loadedIs(n + 1)) ++n;
    else assert(!saved && loadedIs(n));
    // The interrupted position is skipped; the next save still works.
    c = configFor(n + 1);
    assert(CONFIG_Save(&store, &c, NULL));
    boot(); assert(loadedIs(++n));
  }
  assert(cuts >= 5); // Payload pages and header of a save.
  untouchedOutside();
}

static void testChannels(void) {
  boot();
  assert(CHANNEL_STORE_Init() && CHANNEL_STORE_FirstEmpty() == 1);
  for (uint16_t i = 1; i <= USER_CHANNEL_COUNT; ++i) {
    UserChannel ch = channel(145000000u + i * 12500u);
    assert(CHANNEL_STORE_Save(i, &ch));
  }
  boot(); assert(CHANNEL_STORE_Init());
  for (uint16_t i = 1; i <= USER_CHANNEL_COUNT; ++i) assert(channelIs(i, 145000000u + i * 12500u));
  // Overwrite (needs an erase) and delete (clears bits in place).
  UserChannel ch = channel(438500000u);
  uint32_t writesBefore = mutations;
  assert(CHANNEL_STORE_Save(65, &ch) && channelIs(65, 438500000u));
  assert(mutations > writesBefore);
  assert(CHANNEL_STORE_Delete(66) && !CHANNEL_STORE_IsOccupied(66));
  boot(); assert(CHANNEL_STORE_Init() && channelIs(65, 438500000u) && !CHANNEL_STORE_IsOccupied(66));
  ch = channel(145000000u + 66u * 12500u);
  assert(CHANNEL_STORE_Save(66, &ch)); // Refill for the power-fail sweep.

  // Power fails during a save: that channel is old or new, its sector
  // neighbours keep their own value, and saving again succeeds.
  uint32_t hz = 438500000u, cuts = 0;
  for (uint32_t step = 0;; ++step) {
    boot(); assert(CHANNEL_STORE_Init());
    ch = channel(hz + 12500u);
    mutations = 0; cutAt = step;
    (void)CHANNEL_STORE_Save(65, &ch);
    cutAt = UINT32_MAX;
    if (!dead) break; // The save finished before this step: all steps cut.
    ++cuts;
    boot(); assert(CHANNEL_STORE_Init());
    if (channelIs(65, hz + 12500u)) hz += 12500u;
    else assert(channelIs(65, hz));
    for (uint16_t i = 1; i <= USER_CHANNEL_COUNT; ++i)
      if (i != 65) assert(channelIs(i, 145000000u + i * 12500u));
    ch = channel(hz + 12500u);
    assert(CHANNEL_STORE_Save(65, &ch));
    hz += 12500u;
  }
  assert(cuts > 60); // Both copies, journal steps included.
  untouchedOutside();
}

int main(void) {
  testDriver();
  testIdentity();
  testConfig();
  testChannels();
  return 0;
}
