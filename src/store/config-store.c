#include "store/config-store.h"
#include "core/crc32.h"
#include <string.h>

// Record: "UVR2", format, band count, slot capacity, pad, sequence, CRC32,
// then the payload below. Two copies; the newer valid one is loaded.
#define RECORD_BYTES (CONFIG_STORE_SLOT_SIZE)
#define PAYLOAD_START 16u
#define HEADER_VERSION 4u
#define HEADER_BANDS 5u
#define HEADER_SLOTS 6u
#define HEADER_SEQUENCE 8u
#define HEADER_CRC 12u
#define BAND_BYTES 16u
#define SLOT_BYTES (3u + BAND_BYTES*(RADIO_STATE_BANDS+1u) + 11u)
#define PRESET_BYTES 10u
#define PREFERENCE_BYTES 21u
#define PAYLOAD_BYTES (2u + SLOT_BYTES*RADIO_SLOT_CAPACITY + PRESET_BYTES*RADIO_STATE_BANDS + PREFERENCE_BYTES)
_Static_assert(PAYLOAD_START + PAYLOAD_BYTES <= RECORD_BYTES, "Configuration must fit in a slot");

static void put16(uint8_t *p, uint16_t n) {
  p[0] = n & 255u; p[1] = n >> 8;
}
static void put32(uint8_t *p, uint32_t n) {
  put16(p, n); put16(p+2, n >> 16);
}
static uint16_t get16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t get32(const uint8_t *p) {
  return (uint32_t)get16(p) | ((uint32_t)get16(p+2) << 16);
}
// The CRC field itself counts as zeros.
static uint32_t crc32(const uint8_t *bytes, uint16_t size) {
  static const uint8_t zero[4];
  uint32_t crc = CRC32_Update(CRC32_INIT, bytes, HEADER_CRC);
  crc = CRC32_Update(crc, zero, sizeof(zero));
  return ~CRC32_Update(crc, bytes + HEADER_CRC + 4, size - HEADER_CRC - 4u);
}
static uint8_t *encodeBand(uint8_t *p, const BandState *band) {
  put32(p, band->frequency_hz);
  put32(p+4, band->parameters.step_hz);
  put32(p+8, band->parameters.bandwidth_hz);
  p[12] = band->parameters.mode;
  p[13] = band->parameters.squelch;
  put16(p+14, band->parameters.rx_tone);
  return p + BAND_BYTES;
}
static const uint8_t *decodeBand(const uint8_t *p, BandState *band) {
  band->frequency_hz = get32(p);
  band->parameters = (RadioParameters){
      .mode = (RxMode)p[12], .step_hz = get32(p+4),
      .bandwidth_hz = get32(p+8), .squelch = p[13], .rx_tone = get16(p+14)};
  return p + BAND_BYTES;
}
void CONFIG_Default(DeviceConfig *config) {
  if (!config)
    return;
  *config = (DeviceConfig){0};
  RADIO_STATE_Default(&config->radio);
  config->preferences = (ConfigPreferences)CONFIG_DEFAULT_PREFERENCES;
}
static bool valid(const DeviceConfig *config) {
  const ConfigPreferences *p = &config->preferences;
  return RADIO_STATE_Valid(&config->radio) &&
         p->tx_timeout_seconds >= 15 && p->tx_timeout_seconds <= 600 &&
         p->tx_busy_lockout <= 1 &&
         p->battery_calibration >= 1900 && p->battery_calibration <= 2154 &&
         p->backlight <= 6 && p->contrast <= 15 && p->brightness <= 15 &&
         p->battery_type <= 2 && p->battery_style <= 2 &&
         p->backlight_on_squelch <= 2 && p->keylock <= 1 &&
         p->ptt_lock <= 1 && p->beep <= 1 && p->si4732_power_off <= 1 &&
         p->mic <= 31 && p->deviation <= 204 &&
          p->bound_240_280 <= 1 &&
         p->brightness_low <= 15 && p->sql_open_time <= 7 &&
         p->sql_close_time <= 3;
}
bool CONFIG_Encode(const DeviceConfig *config, uint32_t sequence,
                   uint8_t bytes[RECORD_BYTES]) {
  if (!config || !bytes || !valid(config)) return false;
  memset(bytes, 0, RECORD_BYTES);
  memcpy(bytes, "UVR2", 4);
  bytes[HEADER_VERSION] = CONFIG_STORE_FORMAT;
  bytes[HEADER_BANDS] = RADIO_STATE_BANDS;
  bytes[HEADER_SLOTS] = RADIO_SLOT_CAPACITY;
  put32(bytes + HEADER_SEQUENCE, sequence);
  uint8_t *p = bytes + PAYLOAD_START;
  *p++ = config->radio.slot_count;
  *p++ = config->radio.selected_slot;
  for (uint8_t id=0; id<RADIO_SLOT_CAPACITY; ++id) {
    const RadioSlot *slot = &config->radio.slots[id];
    *p++ = slot->active_band;
    *p++ = (uint8_t)slot->source;
    *p++ = slot->channel_number;
    p = encodeBand(p, &slot->current);
    for (uint8_t i=0; i<RADIO_STATE_BANDS; ++i)
      p = encodeBand(p, &slot->bands[i]);
    const TxConfig *tx = &slot->tx;
    *p++ = (uint8_t)(tx->enabled | tx->power << 1 | tx->shift << 3);
    put32(p, tx->offset_hz); put32(p+4, tx->split_hz); put16(p+8, tx->tone); p += 10;
  }
  for (uint8_t i=0; i<RADIO_STATE_BANDS; ++i) {
    const RadioParameters *preset = &config->radio.presets[i];
    put32(p, preset->step_hz); put16(p+4, preset->bandwidth_hz);
    p[6] = preset->mode; p[7] = preset->squelch;
    put16(p+8, preset->rx_tone); p += PRESET_BYTES;
  }
  const ConfigPreferences *pref = &config->preferences;
  put16(p, pref->battery_calibration); p += 2;
  const uint8_t values[] = {
    pref->backlight, pref->contrast, pref->brightness, pref->battery_type,
    pref->battery_style, pref->backlight_on_squelch, pref->keylock,
    pref->ptt_lock, pref->beep, pref->si4732_power_off, pref->mic,
    pref->deviation, pref->bound_240_280, pref->brightness_low,
    pref->sql_open_time, pref->sql_close_time
  };
  memcpy(p, values, sizeof(values)); p += sizeof(values);
  put16(p, pref->tx_timeout_seconds); p += 2;
  *p++ = pref->tx_busy_lockout;
  if (p != bytes + PAYLOAD_START + PAYLOAD_BYTES) return false;
  put32(bytes + HEADER_CRC, crc32(bytes, RECORD_BYTES));
  return true;
}
bool CONFIG_Decode(const uint8_t bytes[RECORD_BYTES], DeviceConfig *config,
                   uint32_t *sequence) {
  if (!bytes || !config || memcmp(bytes, "UVR2", 4) ||
      bytes[HEADER_VERSION] != CONFIG_STORE_FORMAT ||
      bytes[HEADER_BANDS] != RADIO_STATE_BANDS || bytes[HEADER_SLOTS] != RADIO_SLOT_CAPACITY ||
      get32(bytes + HEADER_CRC) != crc32(bytes, RECORD_BYTES))
    return false;
  DeviceConfig candidate;
  CONFIG_Default(&candidate);
  const uint8_t *p = bytes + PAYLOAD_START;
  candidate.radio.slot_count = *p++;
  candidate.radio.selected_slot = *p++;
  for (uint8_t id=0; id<RADIO_SLOT_CAPACITY; ++id) {
    RadioSlot *slot = &candidate.radio.slots[id];
    slot->active_band = *p++;
    slot->source = (RadioSource)*p++;
    slot->channel_number = *p++;
    p = decodeBand(p, &slot->current);
    for (uint8_t i=0; i<RADIO_STATE_BANDS; ++i)
      p = decodeBand(p, &slot->bands[i]);
    TxConfig *tx = &slot->tx;
    tx->enabled = *p & 1u; tx->power = (TxPower)(*p >> 1 & 3u); tx->shift = (TxShift)(*p >> 3 & 3u);
    if (*p++ >> 5) return false;
    tx->offset_hz = get32(p); tx->split_hz = get32(p+4); tx->tone = get16(p+8); p += 10;
  }
  for (uint8_t i=0; i<RADIO_STATE_BANDS; ++i) {
    candidate.radio.presets[i] = (RadioParameters){
      .step_hz=get32(p), .bandwidth_hz=get16(p+4),
      .mode=(RxMode)p[6], .squelch=p[7], .rx_tone=get16(p+8)};
    p += PRESET_BYTES;
  }
  ConfigPreferences *pref = &candidate.preferences;
  pref->battery_calibration = get16(p); p += 2;
  uint8_t *values[] = {&pref->backlight, &pref->contrast, &pref->brightness,
    &pref->battery_type, &pref->battery_style, &pref->backlight_on_squelch,
    &pref->keylock, &pref->ptt_lock, &pref->beep, &pref->si4732_power_off,
    &pref->mic, &pref->deviation, &pref->bound_240_280,
    &pref->brightness_low, &pref->sql_open_time, &pref->sql_close_time};
  for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i) *values[i] = *p++;
  pref->tx_timeout_seconds = get16(p); p += 2;
  pref->tx_busy_lockout = *p++;
  if (!valid(&candidate)) return false;
  *config = candidate;
  if (sequence) *sequence = get32(bytes + HEADER_SEQUENCE);
  return true;
}
static bool ready(const ConfigStore *store) {
  return store && store->read;
}
static bool later(uint32_t a, uint32_t b) { return (int32_t)(a-b)>0; }
// Reads one copy into config (scratch on failure).
static bool slotRead(const ConfigStore *store, unsigned slot, DeviceConfig *config,
                     uint32_t *sequence) {
  uint8_t bytes[RECORD_BYTES];
  return store->read(store->context, slot, 0, bytes, RECORD_BYTES) &&
         CONFIG_Decode(bytes, config, sequence);
}
// Index of the newer valid copy, or -1; out (if given) receives it.
static int newest(const ConfigStore *store, DeviceConfig *out, uint32_t *sequence) {
  DeviceConfig candidate;
  uint32_t seq=0, best=0;
  int found=-1;
  for (unsigned i=0; i<CONFIG_STORE_SLOTS; ++i)
    if (slotRead(store, i, &candidate, &seq) && (found<0 || later(seq,best))) {
      if (out) *out=candidate;
      best=seq; found=(int)i;
    }
  if (found>=0 && sequence) *sequence=best;
  return found;
}
bool CONFIG_Load(const ConfigStore *store, DeviceConfig *config, uint32_t *sequence) {
  return ready(store) && config && newest(store, config, sequence) >= 0;
}
// Encodes, writes and reads back in small chunks. Kept out of line so its
// 1 KiB record never shares a stack frame with the decode in newest().
static bool __attribute__((noinline)) writeRecord(const ConfigStore *store,
    const DeviceConfig *config, unsigned slot, uint32_t sequence) {
  uint8_t bytes[RECORD_BYTES];
  if (!CONFIG_Encode(config,sequence,bytes) ||
      !store->write(store->context,slot,bytes,RECORD_BYTES)) return false;
  for (uint16_t at=0; at<RECORD_BYTES; at+=32) {
    uint8_t check[32];
    if (!store->read(store->context,slot,at,check,sizeof(check)) ||
        memcmp(check,bytes+at,sizeof(check))) return false;
  }
  return true;
}
bool CONFIG_Save(const ConfigStore *store, const DeviceConfig *config,
                 uint32_t *sequence) {
  if (!ready(store) || !store->write || !config) return false;
  uint32_t best=0;
  int current=newest(store, NULL, &best);
  uint32_t next=current<0 ? 1u : best+1u;
  if (!writeRecord(store, config, current<0 ? 0 : (unsigned)current^1u, next)) return false;
  if (sequence) *sequence=next;
  return true;
}
