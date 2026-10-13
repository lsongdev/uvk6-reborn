#include "core/channel-store.h"
#include "core/channels.h"
#include "core/radio-model.h"
#include "core/settings.h"
#include "hal/eeprom.h"
#include <string.h>

#define CHANNEL_RECORD_BYTES CHANNEL_STORE_RECORD_BYTES
#define CHANNEL_COPY_COUNT CHANNEL_STORE_COPY_COUNT
#define CHANNEL_RECORD_VERSION 1u
#define CHANNEL_CRC_OFFSET 44u

_Static_assert(CHANNELS_OFFSET + SCANLIST_MAX * CH_SIZE <= CHANNEL_STORE_BASE,
               "User channels must not overlap the reserved legacy channel area");
_Static_assert(CHANNEL_STORE_END <= 0x1e00u || CHANNEL_STORE_BASE >= 0x2000u,
               "User channel storage must not overlap factory calibration");

typedef struct {
  uint32_t sequence;
  uint8_t copy;
  bool occupied;
} ChannelIndex;

static ChannelIndex indexCache[USER_CHANNEL_COUNT];
static bool initialized;

static uint16_t get16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}
static uint32_t get32(const uint8_t *p) {
  return (uint32_t)get16(p) | (uint32_t)get16(p + 2) << 16;
}
static void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
  put16(p, (uint16_t)v);
  put16(p + 2, (uint16_t)(v >> 16));
}
static uint32_t crc32(const uint8_t *p, uint16_t size) {
  uint32_t crc = 0xffffffffu;
  for (uint16_t i = 0; i < size; ++i) {
    uint8_t value = i >= CHANNEL_CRC_OFFSET ? 0 : p[i];
    crc ^= value;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
static uint32_t address(uint16_t number, uint8_t copy) {
  return CHANNEL_STORE_BASE +
      (((uint32_t)number - 1u) * CHANNEL_COPY_COUNT + copy) * CHANNEL_RECORD_BYTES;
}
static bool available(void) {
  uint32_t capacity = EEPROM_DetectSize();
  return capacity >= CHANNEL_STORE_END + PATCH_SIZE;
}
static bool valid(const UserChannel *ch) {
  if (!ch || RADIO_MODEL_BandAt(ch->frequency_hz) == UINT8_MAX ||
      (unsigned)ch->rx.mode >= RX_MODE_COUNT || !ch->rx.step_hz ||
      ch->rx.bandwidth_hz > UINT16_MAX || ch->rx.squelch > 10 ||
      !TONE_Valid(ch->rx.rx_tone) ||
      (ch->rx.rx_tone && ch->rx.mode != RX_NFM) ||
      (unsigned)ch->tx.power > TX_POWER_HIGH ||
      !TONE_Valid(ch->tx.tone) ||
      !TX_MODEL_FrequencyValid(ch->tx.frequency_hz))
    return false;
  return memchr(ch->name, '\0', sizeof(ch->name)) != NULL;
}
static bool decode(const uint8_t bytes[CHANNEL_RECORD_BYTES], uint16_t number,
                   UserChannel *out, uint32_t *sequence, bool *occupied) {
  if (memcmp(bytes, "UCHN", 4) || bytes[4] != CHANNEL_RECORD_VERSION ||
      bytes[5] > 1 || bytes[10] != (uint8_t)number ||
      get32(bytes + CHANNEL_CRC_OFFSET) != crc32(bytes, CHANNEL_RECORD_BYTES))
    return false;
  if (sequence) *sequence = get32(bytes + 6);
  if (occupied) *occupied = bytes[5] != 0;
  if (!bytes[5]) return out == NULL;
  UserChannel ch = {0};
  memcpy(ch.name, bytes + 11, USER_CHANNEL_NAME_MAX);
  ch.name[USER_CHANNEL_NAME_MAX] = '\0';
  ch.frequency_hz = get32(bytes + 21);
  ch.rx.step_hz = get32(bytes + 25);
  ch.rx.bandwidth_hz = get16(bytes + 29);
  ch.rx.mode = (RxMode)bytes[31];
  ch.rx.squelch = bytes[32];
  ch.rx.rx_tone = get16(bytes + 33);
  ch.tx.enabled = bytes[35] != 0;
  ch.tx.power = (TxPower)bytes[36];
  ch.tx.frequency_hz = get32(bytes + 37);
  ch.tx.tone = get16(bytes + 41);
  if (bytes[35] > 1 || !valid(&ch)) return false;
  if (out) *out = ch;
  if (sequence) *sequence = get32(bytes + 6);
  return true;
}
static void encode(uint8_t bytes[CHANNEL_RECORD_BYTES], uint16_t number,
                   const UserChannel *ch, uint32_t sequence) {
  memset(bytes, 0, CHANNEL_RECORD_BYTES);
  memcpy(bytes, "UCHN", 4);
  bytes[4] = CHANNEL_RECORD_VERSION;
  bytes[5] = ch != NULL;
  put32(bytes + 6, sequence);
  bytes[10] = (uint8_t)number;
  if (!ch) {
    put32(bytes + CHANNEL_CRC_OFFSET, crc32(bytes, CHANNEL_RECORD_BYTES));
    return;
  }
  memcpy(bytes + 11, ch->name, USER_CHANNEL_NAME_MAX);
  put32(bytes + 21, ch->frequency_hz);
  put32(bytes + 25, ch->rx.step_hz);
  put16(bytes + 29, (uint16_t)ch->rx.bandwidth_hz);
  bytes[31] = (uint8_t)ch->rx.mode;
  bytes[32] = ch->rx.squelch;
  put16(bytes + 33, ch->rx.rx_tone);
  bytes[35] = ch->tx.enabled;
  bytes[36] = (uint8_t)ch->tx.power;
  put32(bytes + 37, ch->tx.frequency_hz);
  put16(bytes + 41, ch->tx.tone);
  put32(bytes + CHANNEL_CRC_OFFSET, crc32(bytes, CHANNEL_RECORD_BYTES));
}
static bool readCopy(uint16_t number, uint8_t copy, UserChannel *out,
                     uint32_t *sequence, bool *occupied) {
  uint8_t bytes[CHANNEL_RECORD_BYTES];
  return EEPROM_ReadBuffer(address(number, copy), bytes, sizeof(bytes)) == 0 &&
      decode(bytes, number, out, sequence, occupied);
}
static bool later(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }
static bool equalChannel(const UserChannel *a, const UserChannel *b) {
  return !memcmp(a->name, b->name, sizeof(a->name)) &&
      a->frequency_hz == b->frequency_hz &&
      a->rx.mode == b->rx.mode && a->rx.step_hz == b->rx.step_hz &&
      a->rx.bandwidth_hz == b->rx.bandwidth_hz &&
      a->rx.squelch == b->rx.squelch && a->rx.rx_tone == b->rx.rx_tone &&
      a->tx.enabled == b->tx.enabled && a->tx.power == b->tx.power &&
      a->tx.frequency_hz == b->tx.frequency_hz && a->tx.tone == b->tx.tone;
}

bool CHANNEL_STORE_Init(void) {
  initialized = false;
  if (!available()) return false;
  memset(indexCache, 0, sizeof(indexCache));
  for (uint16_t n = 1; n <= USER_CHANNEL_COUNT; ++n) {
    uint32_t seq[CHANNEL_COPY_COUNT] = {0};
    bool good[CHANNEL_COPY_COUNT], occupied[CHANNEL_COPY_COUNT];
    good[0] = readCopy(n, 0, NULL, &seq[0], &occupied[0]);
    good[1] = readCopy(n, 1, NULL, &seq[1], &occupied[1]);
    if (good[0] || good[1]) {
      uint8_t best = good[0] && good[1] ? (later(seq[1], seq[0]) ? 1 : 0)
                                        : (good[1] ? 1 : 0);
      indexCache[n - 1] = (ChannelIndex){seq[best], best, occupied[best]};
    }
  }
  initialized = true;
  return true;
}
bool CHANNEL_STORE_Available(void) { return initialized; }
bool CHANNEL_STORE_IsOccupied(uint16_t number) {
  return initialized && number >= 1 && number <= USER_CHANNEL_COUNT &&
      indexCache[number - 1].occupied;
}
bool CHANNEL_STORE_Load(uint16_t number, UserChannel *channel) {
  if (!CHANNEL_STORE_IsOccupied(number) || !channel) return false;
  ChannelIndex *entry = &indexCache[number - 1];
  if (readCopy(number, entry->copy, channel, &entry->sequence, NULL)) return true;
  // A later read failure invalidates the cache and permits trying its twin.
  uint8_t other = entry->copy ^ 1u;
  uint32_t sequence;
  if (!readCopy(number, other, channel, &sequence, NULL)) {
    entry->occupied = false;
    return false;
  }
  *entry = (ChannelIndex){sequence, other, true};
  return true;
}
static bool commit(uint16_t number, const UserChannel *channel) {
  ChannelIndex *entry = &indexCache[number - 1];
  uint8_t target = entry->sequence || entry->occupied ? entry->copy ^ 1u : 0;
  uint32_t sequence = entry->sequence + 1u;
  if (!sequence) sequence = 1; // Zero is reserved for an unused index.
  uint8_t bytes[CHANNEL_RECORD_BYTES];
  UserChannel verify;
  uint32_t verifiedSequence;
  bool occupied;
  encode(bytes, number, channel, sequence);
  if (!EEPROM_TryWriteBuffer(address(number, target), bytes, sizeof(bytes)) ||
      !readCopy(number, target, channel ? &verify : NULL, &verifiedSequence, &occupied) ||
      verifiedSequence != sequence || occupied != (channel != NULL) ||
      (channel && !equalChannel(&verify, channel))) return false;
  *entry = (ChannelIndex){sequence, target, channel != NULL};
  return true;
}
bool CHANNEL_STORE_Save(uint16_t number, const UserChannel *channel) {
  return initialized && number >= 1 && number <= USER_CHANNEL_COUNT &&
      valid(channel) && commit(number, channel);
}
// A newer CRC-protected tombstone prevents the older live copy from returning
// after reboot; never erase both copies in place.
bool CHANNEL_STORE_Delete(uint16_t number) {
  return CHANNEL_STORE_IsOccupied(number) && commit(number, NULL);
}
uint16_t CHANNEL_STORE_FirstEmpty(void) {
  if (!initialized) return 0;
  for (uint16_t n = 1; n <= USER_CHANNEL_COUNT; ++n)
    if (!indexCache[n - 1].occupied) return n;
  return 0;
}
bool CHANNEL_STORE_Next(uint16_t number, bool up, uint16_t *next) {
  if (!initialized || !next || number < 1 || number > USER_CHANNEL_COUNT)
    return false;
  for (uint16_t offset = 1; offset <= USER_CHANNEL_COUNT; ++offset) {
    uint16_t candidate = up
        ? (uint16_t)(((number - 1u + offset) % USER_CHANNEL_COUNT) + 1u)
        : (uint16_t)(((number - 1u + USER_CHANNEL_COUNT - offset) % USER_CHANNEL_COUNT) + 1u);
    if (indexCache[candidate - 1].occupied) {
      *next = candidate;
      return true;
    }
  }
  return false;
}
