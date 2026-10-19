#include "store/channel-store.h"
#include "core/crc32.h"
#include "core/radio-model.h"
#include "hal/storage.h"
#include <string.h>

// Record: tag, sequence, number, flags, name[16], RX (14 B), TX (11 B),
// reserved, CRC32 of bytes 0..59. Start-up reads only tag and sequence.
#define RECORD CHANNEL_STORE_RECORD_BYTES
#define TAG 0xC5u
#define CRC_AT (RECORD - 4u)
#define NAME_AT 4u
#define NAME_LEN (USER_CHANNEL_NAME_BYTES - 1u)

static uint8_t occupied[(USER_CHANNEL_COUNT + 7) / 8], newer[(USER_CHANNEL_COUNT + 7) / 8];
static bool initialized;

static bool bit(const uint8_t *map, uint16_t n) { return map[(n-1u) >> 3] & (1u << ((n-1u) & 7)); }
static void setBit(uint8_t *map, uint16_t n, bool on) {
  uint8_t mask = 1u << ((n-1u) & 7);
  if (on) map[(n-1u) >> 3] |= mask; else map[(n-1u) >> 3] &= ~mask;
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static uint32_t crc32(const uint8_t *p) { return ~CRC32_Update(CRC32_INIT, p, CRC_AT); }
static StorageArea area(uint8_t copy) { return copy ? STORAGE_CHANNELS_B : STORAGE_CHANNELS_A; }
static uint32_t offset(uint16_t number) { return ((uint32_t)number - 1u) * RECORD; }
static bool valid(const UserChannel *ch) {
  return ch && RADIO_MODEL_BandAt(ch->frequency_hz) != UINT8_MAX &&
      (unsigned)ch->rx.mode < RX_MODE_COUNT && ch->rx.step_hz &&
      ch->rx.bandwidth_hz <= UINT16_MAX && ch->rx.squelch <= 10 &&
      TONE_Valid(ch->rx.rx_tone) && (!ch->rx.rx_tone || ch->rx.mode == RX_NFM) &&
      TX_MODEL_ConfigValid(&ch->tx) && memchr(ch->name, '\0', sizeof(ch->name));
}
static bool decode(const uint8_t b[RECORD], uint16_t number, UserChannel *out) {
  if (b[0] != TAG || b[2] != number || get32(b + CRC_AT) != crc32(b)) return false;
  UserChannel ch = {0};
  memcpy(ch.name, b + NAME_AT, NAME_LEN);
  const uint8_t *p = b + NAME_AT + NAME_LEN;
  ch.frequency_hz = get32(p);
  ch.rx.step_hz = get32(p + 4);
  ch.rx.bandwidth_hz = get16(p + 8);
  ch.rx.mode = (RxMode)p[10];
  ch.rx.squelch = p[11];
  ch.rx.rx_tone = get16(p + 12);
  uint8_t tx = p[14];
  ch.tx.enabled = tx & 1u;
  ch.tx.power = (TxPower)(tx >> 1 & 3u);
  ch.tx.shift = (TxShift)(tx >> 3 & 3u);
  ch.tx.offset_hz = get32(p + 15);
  ch.tx.split_hz = get32(p + 19);
  ch.tx.tone = get16(p + 23);
  if (!valid(&ch)) return false;
  if (out) *out = ch;
  return true;
}
static void encode(uint8_t b[RECORD], uint16_t number, uint8_t sequence, const UserChannel *ch) {
  memset(b, 0, RECORD);
  b[0] = TAG;
  b[1] = sequence;
  b[2] = (uint8_t)number;
  memcpy(b + NAME_AT, ch->name, NAME_LEN);
  uint8_t *p = b + NAME_AT + NAME_LEN;
  put32(p, ch->frequency_hz);
  put32(p + 4, ch->rx.step_hz);
  put16(p + 8, (uint16_t)ch->rx.bandwidth_hz);
  p[10] = (uint8_t)ch->rx.mode;
  p[11] = ch->rx.squelch;
  put16(p + 12, ch->rx.rx_tone);
  p[14] = (uint8_t)(ch->tx.enabled | ch->tx.power << 1 | ch->tx.shift << 3);
  put32(p + 15, ch->tx.offset_hz);
  put32(p + 19, ch->tx.split_hz);
  put16(p + 23, ch->tx.tone);
  put32(b + CRC_AT, crc32(b));
}
static bool readCopy(uint16_t number, uint8_t copy, UserChannel *out) {
  uint8_t b[RECORD];
  return STORAGE_Read(area(copy), offset(number), b, RECORD) && decode(b, number, out);
}
// Tag and sequence of one copy; false when the copy is not a channel.
static bool header(uint16_t number, uint8_t copy, uint8_t *sequence) {
  uint8_t h[2];
  if (!STORAGE_Read(area(copy), offset(number), h, 2) || h[0] != TAG) return false;
  *sequence = h[1];
  return true;
}
static uint8_t current(uint16_t number) { return bit(newer, number) ? 1 : 0; }
// Field by field: names compare up to their terminator, padding is ignored.
static bool same(const UserChannel *a, const UserChannel *b) {
  return !strncmp(a->name, b->name, NAME_LEN) && a->frequency_hz == b->frequency_hz &&
      a->rx.mode == b->rx.mode && a->rx.step_hz == b->rx.step_hz &&
      a->rx.bandwidth_hz == b->rx.bandwidth_hz && a->rx.squelch == b->rx.squelch &&
      a->rx.rx_tone == b->rx.rx_tone && a->tx.enabled == b->tx.enabled &&
      a->tx.power == b->tx.power && a->tx.shift == b->tx.shift &&
      a->tx.offset_hz == b->tx.offset_hz && a->tx.split_hz == b->tx.split_hz &&
      a->tx.tone == b->tx.tone;
}

bool CHANNEL_STORE_Init(void) {
  initialized = false;
  if (STORAGE_Size(STORAGE_CHANNELS_A) < CHANNEL_STORE_AREA_BYTES ||
      STORAGE_Size(STORAGE_CHANNELS_B) < CHANNEL_STORE_AREA_BYTES) return false;
  memset(occupied, 0, sizeof(occupied));
  memset(newer, 0, sizeof(newer));
  for (uint16_t n = 1; n <= USER_CHANNEL_COUNT; ++n) {
    uint8_t s0, s1;
    bool a = header(n, 0, &s0), b = header(n, 1, &s1);
    setBit(occupied, n, a || b);
    setBit(newer, n, b && (!a || (int8_t)(s1 - s0) > 0));
  }
  initialized = true;
  return true;
}
bool CHANNEL_STORE_Available(void) { return initialized; }
bool CHANNEL_STORE_IsOccupied(uint16_t number) {
  return initialized && number >= 1 && number <= USER_CHANNEL_COUNT && bit(occupied, number);
}
// The newer copy wins; a corrupt one falls back to its twin.
bool CHANNEL_STORE_Load(uint16_t number, UserChannel *channel) {
  if (!CHANNEL_STORE_IsOccupied(number) || !channel) return false;
  uint8_t copy = current(number);
  if (readCopy(number, copy, channel)) return true;
  if (readCopy(number, copy ^ 1u, channel)) { setBit(newer, number, !copy); return true; }
  setBit(occupied, number, false);
  return false;
}
static bool writeCopy(uint16_t number, uint8_t copy, const uint8_t b[RECORD],
                      const UserChannel *channel) {
  UserChannel verify;
  return STORAGE_Write(area(copy), offset(number), b, RECORD) &&
         readCopy(number, copy, &verify) && same(&verify, channel);
}
bool CHANNEL_STORE_Save(uint16_t number, const UserChannel *channel) {
  if (!initialized || number < 1 || number > USER_CHANNEL_COUNT || !valid(channel))
    return false;
  uint8_t copy = current(number), sequence = 0, target = 0;
  if (bit(occupied, number) && header(number, copy, &sequence)) target = copy ^ 1u;
  uint8_t b[RECORD];
  encode(b, number, (uint8_t)(sequence + 1u), channel);
  if (!writeCopy(number, target, b, channel)) return false;
  setBit(occupied, number, true);
  setBit(newer, number, target);
  // The new version is safe; now make the other copy match it.
  return writeCopy(number, target ^ 1u, b, channel);
}
bool CHANNEL_STORE_Delete(uint16_t number) {
  if (!CHANNEL_STORE_IsOccupied(number)) return false;
  static const uint8_t erased = 0;
  for (uint8_t copy = 0; copy < CHANNEL_STORE_COPY_COUNT; ++copy)
    if (!STORAGE_Write(area(copy), offset(number), &erased, 1)) return false;
  setBit(occupied, number, false);
  return true;
}
uint16_t CHANNEL_STORE_FirstEmpty(void) {
  if (!initialized) return 0;
  for (uint16_t n = 1; n <= USER_CHANNEL_COUNT; ++n)
    if (!bit(occupied, n)) return n;
  return 0;
}
bool CHANNEL_STORE_Next(uint16_t number, bool up, uint16_t *next) {
  if (!initialized || !next || number < 1 || number > USER_CHANNEL_COUNT) return false;
  for (uint16_t step = 1; step <= USER_CHANNEL_COUNT; ++step) {
    uint16_t n = up ? (uint16_t)((number - 1u + step) % USER_CHANNEL_COUNT + 1u)
                    : (uint16_t)((number - 1u + USER_CHANNEL_COUNT - step) % USER_CHANNEL_COUNT + 1u);
    if (bit(occupied, n)) { *next = n; return true; }
  }
  return false;
}
