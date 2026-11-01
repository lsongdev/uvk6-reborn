#include "store/channel-store.h"
#include "core/crc32.h"
#include "core/fields.h"
#include "core/radio-model.h"
#include "core/tone.h"
#include "hal/storage.h"
#include <string.h>

// Record: tag, sequence, number, flags, the 41-byte normalized channel,
// reserved, CRC32 of bytes 0..59. Start-up reads only tag and sequence.
// Records of earlier formats (tag 0xC5) read as empty.
#define RECORD CHANNEL_STORE_RECORD_BYTES
#define TAG 0xC6u
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
// Tones as type << 12 | value: 1 CTCSS in 0.1 Hz, 2 DCS normal and
// 3 DCS inverted with the octal code's value, 0 off. Zero for no match.
static RadioTone toneFrom(uint16_t code) {
  uint16_t value = code & 0xFFFu;
  switch (code >> 12) {
  case 0: return value ? RADIO_TONE_COUNT : 0;
  case 1:
    for (uint8_t i = 0; i < 50; ++i) if (CTCSS_Options[i] == value) return (RadioTone)(1u + i);
    break;
  case 2: case 3:
    for (uint8_t i = 0; i < 104; ++i)
      if (DCS_Options[i] == value) return (RadioTone)((code >> 12 == 2 ? 51u : 155u) + i);
    break;
  }
  return RADIO_TONE_COUNT; // Invalid: refused by valid().
}
static uint16_t toneTo(RadioTone tone) {
  DCS_CodeType_t type = TONE_Type(tone);
  uint8_t code = TONE_Code(tone);
  return type == CODE_TYPE_OFF ? 0 : type == CODE_TYPE_CONTINUOUS_TONE ? 0x1000u | CTCSS_Options[code] :
      (type == CODE_TYPE_DIGITAL ? 0x2000u : 0x3000u) | DCS_Options[code];
}
// After the name: RX, the TX flags byte (TX_MODEL_Flags), then TX.
static const Field rxFields[] = {
  FIELD(UserChannel, frequency_hz, 4), FIELD(UserChannel, rx.step_hz, 4),
  FIELD(UserChannel, rx.bandwidth_hz, 2), FIELD(UserChannel, rx.mode, 1),
  FIELD(UserChannel, rx.squelch, 1), FIELD(UserChannel, rx.rx_tone, 2),
};
static const Field txFields[] = {
  FIELD(UserChannel, tx.offset_hz, 4), FIELD(UserChannel, tx.split_hz, 4),
  FIELD(UserChannel, tx.tone, 2),
};
static bool decodeFields(const uint8_t *p, UserChannel *out) {
  UserChannel ch = {0};
  memcpy(ch.name, p, NAME_LEN);
  uint8_t *at = FIELDS_CODE((uint8_t *)p + NAME_LEN, &ch, rxFields, true); // Only reads p.
  if (!TX_MODEL_SetFlags(&ch.tx, *at)) return false;
  FIELDS_CODE(at + 1, &ch, txFields, true);
  ch.rx.rx_tone = toneFrom(ch.rx.rx_tone);
  ch.tx.tone = toneFrom(ch.tx.tone);
  if (!valid(&ch)) return false;
  if (out) *out = ch;
  return true;
}
bool CHANNEL_STORE_Decode(const uint8_t record[CHANNEL_RECORD_BYTES], UserChannel *channel) {
  return decodeFields(record, channel);
}
void CHANNEL_STORE_Encode(const UserChannel *channel, uint8_t record[CHANNEL_RECORD_BYTES]) {
  UserChannel ch = *channel;
  ch.rx.rx_tone = toneTo(ch.rx.rx_tone);
  ch.tx.tone = toneTo(ch.tx.tone);
  memset(record, 0, CHANNEL_RECORD_BYTES);
  strncpy((char *)record, ch.name, NAME_LEN); // Zero after the name.
  uint8_t *at = FIELDS_CODE(record + NAME_LEN, &ch, rxFields, false);
  *at = TX_MODEL_Flags(&ch.tx);
  FIELDS_CODE(at + 1, &ch, txFields, false);
}
static bool decode(const uint8_t b[RECORD], uint16_t number, UserChannel *out) {
  return b[0] == TAG && b[2] == number && get32(b + CRC_AT) == crc32(b) &&
      decodeFields(b + NAME_AT, out);
}
static void encode(uint8_t b[RECORD], uint16_t number, uint8_t sequence, const UserChannel *ch) {
  memset(b, 0, RECORD);
  b[0] = TAG;
  b[1] = sequence;
  b[2] = (uint8_t)number;
  CHANNEL_STORE_Encode(ch, b + NAME_AT);
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
// Reads the copy back: it must hold exactly the record just written.
static bool writeCopy(uint16_t number, uint8_t copy, const uint8_t b[RECORD]) {
  uint8_t check[RECORD];
  return STORAGE_Write(area(copy), offset(number), b, RECORD) &&
         STORAGE_Read(area(copy), offset(number), check, RECORD) && !memcmp(check, b, RECORD);
}
bool CHANNEL_STORE_Save(uint16_t number, const UserChannel *channel) {
  if (!initialized || number < 1 || number > USER_CHANNEL_COUNT || !valid(channel))
    return false;
  uint8_t copy = current(number), sequence = 0, target = 0;
  if (bit(occupied, number) && header(number, copy, &sequence)) target = copy ^ 1u;
  uint8_t b[RECORD];
  encode(b, number, (uint8_t)(sequence + 1u), channel);
  if (!writeCopy(number, target, b)) return false;
  setBit(occupied, number, true);
  setBit(newer, number, target);
  // The new version is safe; now make the other copy match it.
  return writeCopy(number, target ^ 1u, b);
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
