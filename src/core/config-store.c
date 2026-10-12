#include "core/config-store.h"
#include <string.h>

#define RECORD_BYTES (CONFIG_STORE_SLOT_SIZE)
#define PAYLOAD_START 16u
#define HEADER_VERSION 4u
#define HEADER_BANDS 5u
#define HEADER_SEQUENCE 8u
#define HEADER_CRC 12u
_Static_assert(PAYLOAD_START + 1 + 14*(RADIO_STATE_BANDS+1) + 18 + 8*RADIO_STATE_BANDS <= RECORD_BYTES,
               "Band presets must fit in the existing configuration slot");

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
static uint32_t crc32(const uint8_t *bytes) {
  uint32_t crc = 0xffffffffu;
  for (unsigned i = 0; i < RECORD_BYTES; ++i) {
    uint8_t value = i >= HEADER_CRC && i < HEADER_CRC+4 ? 0 : bytes[i];
    crc ^= value;
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
static uint8_t *encodeBand(uint8_t *out, const BandState *band) {
  put32(out, band->frequency_hz);
  put32(out+4, band->parameters.step_hz);
  put32(out+8, band->parameters.bandwidth_hz);
  out[12] = band->parameters.mode;
  out[13] = band->parameters.squelch;
  return out + 14;
}
static const uint8_t *decodeBand(const uint8_t *in, BandState *band) {
  band->frequency_hz = get32(in);
  band->parameters = (RadioParameters){
      .mode = (RxMode)in[12], .step_hz = get32(in+4),
      .bandwidth_hz = get32(in+8), .squelch = in[13], .tx_enabled = false};
  return in + 14;
}
void CONFIG_Default(DeviceConfig *config) {
  if (!config)
    return;
  *config = (DeviceConfig){0};
  RADIO_STATE_Default(&config->radio);
  config->preferences = (ConfigPreferences){
      .battery_calibration = 2000, .backlight = 3, .contrast = 8,
      .brightness = 8, .battery_type = 0, .battery_style = 1,
      .backlight_on_squelch = 1, .mic = 0, .deviation = 130,
      .brightness_low = 0, .sql_open_time = 1, .sql_close_time = 1};
}
static bool valid(const DeviceConfig *config) {
  const ConfigPreferences *p = &config->preferences;
  return RADIO_STATE_Valid(&config->radio) &&
         p->battery_calibration >= 1900 && p->battery_calibration <= 2154 &&
         p->backlight <= 6 && p->contrast <= 15 && p->brightness <= 15 &&
         p->battery_type <= 2 && p->battery_style <= 2 &&
         p->backlight_on_squelch <= 2 && p->keylock <= 1 &&
         p->ptt_lock <= 1 && p->beep <= 1 && p->si4732_power_off <= 1 &&
         p->mic <= 15 && p->bound_240_280 <= 1 &&
         p->brightness_low <= 15 && p->sql_open_time <= 7 &&
         p->sql_close_time <= 3;
}
bool CONFIG_Encode(const DeviceConfig *config, uint32_t sequence,
                   uint8_t bytes[RECORD_BYTES]) {
  if (!config || !bytes || !valid(config))
    return false;
  memset(bytes, 0, RECORD_BYTES);
  memcpy(bytes, "UVR2", 4);
  bytes[HEADER_VERSION] = CONFIG_STORE_FORMAT;
  bytes[HEADER_BANDS] = RADIO_STATE_BANDS;
  put32(bytes + HEADER_SEQUENCE, sequence);
  uint8_t *p = bytes + PAYLOAD_START;
  *p++ = config->radio.active_band;
  p = encodeBand(p, &config->radio.current);
  for (uint8_t i = 0; i < RADIO_STATE_BANDS; ++i)
    p = encodeBand(p, &config->radio.bands[i]);
  const ConfigPreferences *pref = &config->preferences;
  put16(p, pref->battery_calibration); p += 2;
  const uint8_t values[] = {
    pref->backlight, pref->contrast, pref->brightness, pref->battery_type,
    pref->battery_style, pref->backlight_on_squelch, pref->keylock,
    pref->ptt_lock, pref->beep, pref->si4732_power_off, pref->mic,
    pref->deviation, pref->bound_240_280, pref->brightness_low,
    pref->sql_open_time, pref->sql_close_time
  };
  memcpy(p, values, sizeof(values));
  p += sizeof(values);
  for (uint8_t i = 0; i < RADIO_STATE_BANDS; ++i) {
    const RadioParameters *preset = &config->radio.presets[i];
    put32(p, preset->step_hz);
    put16(p+4, preset->bandwidth_hz);
    p[6] = preset->mode; p[7] = preset->squelch;
    p += 8;
  }
  put32(bytes + HEADER_CRC, crc32(bytes));
  return true;
}
bool CONFIG_Decode(const uint8_t bytes[RECORD_BYTES], DeviceConfig *config,
                   uint32_t *sequence) {
  if (!bytes || !config || memcmp(bytes, "UVR2", 4) != 0 ||
      bytes[HEADER_VERSION] != CONFIG_STORE_FORMAT ||
      bytes[HEADER_BANDS] != RADIO_STATE_BANDS ||
      get32(bytes + HEADER_CRC) != crc32(bytes))
    return false;
  DeviceConfig candidate = {0};
  const uint8_t *p = bytes + PAYLOAD_START;
  candidate.radio.active_band = *p++;
  p = decodeBand(p, &candidate.radio.current);
  for (uint8_t i = 0; i < RADIO_STATE_BANDS; ++i)
    p = decodeBand(p, &candidate.radio.bands[i]);
  ConfigPreferences *pref = &candidate.preferences;
  pref->battery_calibration = get16(p); p += 2;
  uint8_t *values[] = {&pref->backlight, &pref->contrast, &pref->brightness,
    &pref->battery_type, &pref->battery_style, &pref->backlight_on_squelch,
    &pref->keylock, &pref->ptt_lock, &pref->beep, &pref->si4732_power_off,
    &pref->mic, &pref->deviation, &pref->bound_240_280,
    &pref->brightness_low, &pref->sql_open_time, &pref->sql_close_time};
  for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
    *values[i] = *p++;
  for (uint8_t i = 0; i < RADIO_STATE_BANDS; ++i) {
    candidate.radio.presets[i] = (RadioParameters){
        .step_hz = get32(p), .bandwidth_hz = get16(p+4),
        .mode = (RxMode)p[6], .squelch = p[7], .tx_enabled = false};
    p += 8;
  }
  if (!valid(&candidate))
    return false;
  *config = candidate;
  if (sequence)
    *sequence = get32(bytes + HEADER_SEQUENCE);
  return true;
}
static bool ready(const ConfigStore *store) {
  return store && store->read &&
         store->capacity >= CONFIG_STORE_SLOT_SIZE*CONFIG_STORE_SLOTS &&
         store->reserved_tail <= store->capacity -
               CONFIG_STORE_SLOT_SIZE*CONFIG_STORE_SLOTS;
}
static int slotRead(const ConfigStore *store, unsigned slot, DeviceConfig *out,
                    uint32_t *seq) {
  uint8_t bytes[RECORD_BYTES];
  return store->read(store->context, slot*RECORD_BYTES, bytes, RECORD_BYTES) &&
         CONFIG_Decode(bytes, out, seq);
}
static bool later(uint32_t a, uint32_t b) {
  return (int32_t)(a - b) > 0;
}
bool CONFIG_Load(const ConfigStore *store, DeviceConfig *config, uint32_t *sequence) {
  if (!ready(store) || !config)
    return false;
  DeviceConfig a, b;
  uint32_t seqA = 0, seqB = 0;
  bool okA = slotRead(store, 0, &a, &seqA);
  bool okB = slotRead(store, 1, &b, &seqB);
  if (!okA && !okB)
    return false;
  bool chooseB = okB && (!okA || later(seqB, seqA));
  *config = chooseB ? b : a;
  if (sequence) *sequence = chooseB ? seqB : seqA;
  return true;
}
bool CONFIG_Save(const ConfigStore *store, const DeviceConfig *config,
                 uint32_t *sequence) {
  if (!ready(store) || !store->write || !config)
    return false;
  DeviceConfig a, b;
  uint32_t seqA = 0, seqB = 0;
  bool okA = slotRead(store, 0, &a, &seqA);
  bool okB = slotRead(store, 1, &b, &seqB);
  bool newestB = okB && (!okA || later(seqB, seqA));
  unsigned target = !okA && !okB ? 0 : (newestB ? 0 : 1);
  uint32_t nextSeq = !okA && !okB ? 1 : (newestB ? seqB : seqA)+1;
  uint8_t bytes[RECORD_BYTES];
  if (!CONFIG_Encode(config, nextSeq, bytes) ||
      !store->write(store->context, target*RECORD_BYTES, bytes, RECORD_BYTES))
    return false;
  DeviceConfig verify;
  uint32_t verifySeq;
  if (!slotRead(store, target, &verify, &verifySeq) || verifySeq != nextSeq)
    return false;
  if (sequence) *sequence = nextSeq;
  return true;
}
