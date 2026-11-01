#include "store/config-store.h"
#include "core/crc32.h"
#include "core/fields.h"
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
static const Field bandFields[] = {
  FIELD(BandState, frequency_hz, 4), FIELD(BandState, parameters.step_hz, 4),
  FIELD(BandState, parameters.bandwidth_hz, 4), FIELD(BandState, parameters.mode, 1),
  FIELD(BandState, parameters.squelch, 1), FIELD(BandState, parameters.rx_tone, 2),
};
static const Field slotFields[] = {
  FIELD(RadioSlot, active_band, 1), FIELD(RadioSlot, source, 1), FIELD(RadioSlot, channel_number, 1),
};
static const Field txFields[] = { // After the flags byte.
  FIELD(TxConfig, offset_hz, 4), FIELD(TxConfig, split_hz, 4), FIELD(TxConfig, tone, 2),
};
static const Field presetFields[] = {
  FIELD(RadioParameters, step_hz, 4), FIELD(RadioParameters, bandwidth_hz, 2),
  FIELD(RadioParameters, mode, 1), FIELD(RadioParameters, squelch, 1),
  FIELD(RadioParameters, rx_tone, 2),
};
#define PREF(member, width) FIELD(ConfigPreferences, member, width)
static const Field preferenceFields[] = {
  PREF(battery_calibration, 2), PREF(backlight, 1), PREF(contrast, 1), PREF(brightness, 1),
  PREF(battery_type, 1), PREF(battery_style, 1), PREF(backlight_on_squelch, 1),
  PREF(keylock, 1), PREF(ptt_lock, 1), PREF(beep, 1), PREF(si4732_power_off, 1),
  PREF(mic, 1), PREF(deviation, 1), PREF(bound_240_280, 1), PREF(brightness_low, 1),
  PREF(sql_open_time, 1), PREF(sql_close_time, 1), PREF(tx_timeout_seconds, 2),
  PREF(tx_busy_lockout, 1),
};
// The payload after the header, either way: its end, or NULL on a reserved
// TX flag bit. Encoding only reads the config, decoding only the bytes.
static uint8_t *payload(uint8_t *p, DeviceConfig *config, bool decode) {
  RadioUserState *radio = &config->radio;
  p = FIELDS_Code(p, radio, (const Field[]){FIELD(RadioUserState, slot_count, 1),
                                            FIELD(RadioUserState, selected_slot, 1)}, 2, decode);
  for (uint8_t id=0; id<RADIO_SLOT_CAPACITY; ++id) {
    RadioSlot *slot = &radio->slots[id];
    p = FIELDS_CODE(p, slot, slotFields, decode);
    p = FIELDS_CODE(p, &slot->current, bandFields, decode);
    for (uint8_t i=0; i<RADIO_STATE_BANDS; ++i)
      p = FIELDS_CODE(p, &slot->bands[i], bandFields, decode);
    TxConfig *tx = &slot->tx;
    if (!decode) *p = TX_MODEL_Flags(tx);
    else if (!TX_MODEL_SetFlags(tx, *p)) return NULL;
    p = FIELDS_CODE(p + 1, tx, txFields, decode);
  }
  for (uint8_t i=0; i<RADIO_STATE_BANDS; ++i)
    p = FIELDS_CODE(p, &radio->presets[i], presetFields, decode);
  return FIELDS_CODE(p, &config->preferences, preferenceFields, decode);
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
         p->battery_calibration >= CALIBRATION_BATTERY_MIN &&
         p->battery_calibration <= CALIBRATION_BATTERY_MAX &&
         p->backlight <= 6 && p->contrast <= 15 && p->brightness <= 15 &&
         p->battery_type <= 2 && p->battery_style <= 2 &&
         p->backlight_on_squelch <= 2 && p->keylock <= 1 &&
         p->ptt_lock <= 1 && p->beep <= 1 && p->si4732_power_off <= 1 &&
         p->mic <= 31 && p->deviation <= 204 &&
          p->bound_240_280 <= 1 &&
         p->brightness_low <= 15 && p->sql_open_time <= PREFERENCES_SQL_OPEN_AUTO &&
         p->sql_close_time <= PREFERENCES_SQL_CLOSE_AUTO;
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
  if (payload(bytes + PAYLOAD_START, (DeviceConfig *)config, false) !=
      bytes + PAYLOAD_START + PAYLOAD_BYTES) return false;
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
  if (payload((uint8_t *)bytes + PAYLOAD_START, &candidate, true) !=
      bytes + PAYLOAD_START + PAYLOAD_BYTES) return false;
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
