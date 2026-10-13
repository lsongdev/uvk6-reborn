#include "core/config-store.h"
#include <string.h>

#define RECORD_BYTES (CONFIG_STORE_SLOT_SIZE)
#define PAYLOAD_START 16u
#define HEADER_VERSION 4u
#define HEADER_BANDS 5u
#define HEADER_SEQUENCE 8u
#define HEADER_CRC 12u
#define FORMAT7_BYTES (PAYLOAD_START + 3u + RADIO_SLOT_CAPACITY*(2u + 16u*(RADIO_STATE_BANDS+1u) + 8u) + 10u*RADIO_STATE_BANDS + 21u)
#define FORMAT8_BYTES (FORMAT7_BYTES + 2u*RADIO_SLOT_CAPACITY)
_Static_assert(FORMAT7_BYTES == 810u && FORMAT7_BYTES <= RECORD_BYTES,
               "Multi VFO configuration must fit in a slot");
_Static_assert(FORMAT8_BYTES == 818u && FORMAT8_BYTES <= RECORD_BYTES,
               "VFO channel-source state must fit in a slot");
_Static_assert(CONFIG_STORE_END <= 0x1e00u, "Never overlap RF calibration");

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
static uint32_t crc32(const uint8_t *bytes, uint16_t size) {
  uint32_t crc = 0xffffffffu;
  for (unsigned i = 0; i < size; ++i) {
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
      .bandwidth_hz = get32(in+8), .squelch = in[13]};
  return in + 14;
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
static uint8_t *encodeBand6(uint8_t *p, const BandState *band) {
  encodeBand(p, band);
  put16(p+14, band->parameters.rx_tone);
  return p+16;
}
static const uint8_t *decodeBand6(const uint8_t *p, BandState *band) {
  decodeBand(p, band);
  band->parameters.rx_tone = get16(p+14);
  return p+16;
}
bool CONFIG_Encode(const DeviceConfig *config, uint32_t sequence,
                   uint8_t bytes[RECORD_BYTES]) {
  if (!config || !bytes || !valid(config)) return false;
  memset(bytes, 0, RECORD_BYTES);
  memcpy(bytes, "UVR2", 4);
  bytes[HEADER_VERSION] = CONFIG_STORE_FORMAT;
  bytes[HEADER_BANDS] = RADIO_STATE_BANDS;
  put32(bytes + HEADER_SEQUENCE, sequence);
  uint8_t *p = bytes + PAYLOAD_START;
  bytes[6] = RADIO_SLOT_CAPACITY;
  *p++ = config->radio.slot_count;
  *p++ = config->radio.selected_slot;
  *p++ = config->radio.watch_enabled;
  for (uint8_t id=0; id<RADIO_SLOT_CAPACITY; ++id) {
    const RadioSlot *slot = &config->radio.slots[id];
    *p++ = slot->active_band;
    *p++ = slot->watch;
    p = encodeBand6(p, &slot->current);
    for (uint8_t i=0; i<RADIO_STATE_BANDS; ++i)
      p = encodeBand6(p, &slot->bands[i]);
    *p++ = slot->tx.enabled;
    *p++ = slot->tx.power;
    put32(p, slot->tx.frequency_hz); p+=4;
    put16(p, slot->tx.tone); p+=2;
    *p++ = (uint8_t)slot->source;
    *p++ = slot->channel_number;
  }
  for (uint8_t i=0; i<RADIO_STATE_BANDS; ++i) {
    const RadioParameters *preset = &config->radio.presets[i];
    put32(p, preset->step_hz); put16(p+4, preset->bandwidth_hz);
    p[6] = preset->mode; p[7] = preset->squelch;
    put16(p+8, preset->rx_tone); p+=10;
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
  memcpy(p, values, sizeof(values));
  p += sizeof(values);
  put16(p, pref->tx_timeout_seconds); p+=2;
  *p++ = pref->tx_busy_lockout;
  if (p != bytes+FORMAT8_BYTES) return false;
  put32(bytes + HEADER_CRC, crc32(bytes, RECORD_BYTES));
  return true;
}
static const uint8_t *decodePreferences(const uint8_t *p, ConfigPreferences *pref) {
  pref->battery_calibration = get16(p); p += 2;
  uint8_t *values[] = {&pref->backlight, &pref->contrast, &pref->brightness,
    &pref->battery_type, &pref->battery_style, &pref->backlight_on_squelch,
    &pref->keylock, &pref->ptt_lock, &pref->beep, &pref->si4732_power_off,
    &pref->mic, &pref->deviation, &pref->bound_240_280,
    &pref->brightness_low, &pref->sql_open_time, &pref->sql_close_time};
  for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
    *values[i] = *p++;
  return p;
}
bool CONFIG_Decode(const uint8_t *bytes, uint16_t size, DeviceConfig *config,
                   uint32_t *sequence) {
  if (!bytes || !config || size < CONFIG_LEGACY_SLOT_SIZE || memcmp(bytes, "UVR2", 4) != 0 ||
      (bytes[HEADER_VERSION] < 4u || bytes[HEADER_VERSION] > CONFIG_STORE_FORMAT) ||
      bytes[HEADER_BANDS] != RADIO_STATE_BANDS ||
      size != (bytes[HEADER_VERSION] >= 7u ? RECORD_BYTES : CONFIG_LEGACY_SLOT_SIZE) ||
      get32(bytes + HEADER_CRC) != crc32(bytes, size))
    return false;
  DeviceConfig candidate;
  CONFIG_Default(&candidate);
  const uint8_t *p = bytes + PAYLOAD_START;
  if (bytes[HEADER_VERSION] >= 6u) {
    bool fourVfos = bytes[HEADER_VERSION] >= 7u;
    bool channelSources = bytes[HEADER_VERSION] >= 8u;
    uint8_t records = fourVfos ? RADIO_SLOT_CAPACITY : 2;
    if (fourVfos && bytes[6] != RADIO_SLOT_CAPACITY) return false;
    candidate.radio.slot_count = *p++;
    if (!fourVfos && candidate.radio.slot_count != 2) return false;
    candidate.radio.selected_slot = *p++;
    if (fourVfos) {
      if (*p>1) return false;
      candidate.radio.watch_enabled = *p++;
    }
    for (uint8_t id=0; id<records; ++id) {
      RadioSlot *slot = &candidate.radio.slots[id];
      slot->active_band = *p++;
      if (fourVfos) {
        if (*p>1) return false;
        slot->watch = *p++;
      }
      p = decodeBand6(p, &slot->current);
      for (uint8_t i=0; i<RADIO_STATE_BANDS; ++i)
        p = decodeBand6(p, &slot->bands[i]);
      if (p[0] > 1 || p[1] > TX_POWER_HIGH) return false;
      slot->tx.enabled = *p++;
      slot->tx.power = *p++;
      slot->tx.frequency_hz = get32(p); p+=4;
      slot->tx.tone = get16(p); p+=2;
      if (channelSources) {
        slot->source = (RadioSource)*p++;
        slot->channel_number = *p++;
      }
    }
    for (uint8_t i=0; i<RADIO_STATE_BANDS; ++i) {
      candidate.radio.presets[i] = (RadioParameters){
        .step_hz=get32(p), .bandwidth_hz=get16(p+4),
        .mode=p[6], .squelch=p[7], .rx_tone=get16(p+8)};
      p+=10;
    }
    p = decodePreferences(p, &candidate.preferences);
    candidate.preferences.tx_timeout_seconds = get16(p); p+=2;
    candidate.preferences.tx_busy_lockout = *p++;
    if (p != bytes+(channelSources ? FORMAT8_BYTES : fourVfos ? FORMAT7_BYTES : 467u)) return false;
    if (!fourVfos)
      for (uint8_t id=2; id<RADIO_SLOT_CAPACITY; ++id)
        RADIO_STATE_InitSlot(&candidate.radio, id, id%2 ? 6 : 5);
  } else {
    // Legacy format 4/5 stores one VFO. Keep its exact byte offsets.
    RadioSlot *slot = &candidate.radio.slots[0];
    slot->active_band = *p++;
    p = decodeBand(p, &slot->current);
    for (uint8_t i = 0; i < RADIO_STATE_BANDS; ++i)
      p = decodeBand(p, &slot->bands[i]);
    p = decodePreferences(p, &candidate.preferences);
    for (uint8_t i = 0; i < RADIO_STATE_BANDS; ++i) {
      candidate.radio.presets[i] = (RadioParameters){
          .step_hz = get32(p), .bandwidth_hz = get16(p+4),
          .mode = (RxMode)p[6], .squelch = p[7]};
      p += 8;
    }
    if (p[0] > 1 || p[1] > TX_POWER_HIGH) return false;
    slot->tx.enabled = p[0] != 0;
    slot->tx.power = (TxPower)p[1];
    candidate.preferences.tx_timeout_seconds = get16(p+2);
    candidate.preferences.tx_busy_lockout = p[4];
    if (bytes[HEADER_VERSION] == 5u) {
      p += 5;
      slot->current.parameters.rx_tone = get16(p); p += 2;
      for (uint8_t i=0; i<RADIO_STATE_BANDS; ++i) {
        slot->bands[i].parameters.rx_tone = get16(p); p += 2;
        candidate.radio.presets[i].rx_tone = get16(p); p += 2;
      }
      slot->tx.tone = get16(p); p += 2;
      slot->tx.frequency_hz = get32(p);
    } // Format 4 defaults the new fields to Off/Same.
    for (uint8_t id=1; id<RADIO_SLOT_CAPACITY; ++id) {
      RADIO_STATE_InitSlot(&candidate.radio, id, id%2 ? 6 : 5);
      candidate.radio.slots[id].watch = id<2;
    }
  }
  if (!valid(&candidate))
    return false;
  *config = candidate;
  if (sequence)
    *sequence = get32(bytes + HEADER_SEQUENCE);
  return true;
}
static bool ready(const ConfigStore *store) {
  return store && store->read && store->capacity >= CONFIG_STORE_END &&
         store->reserved_tail <= store->capacity-CONFIG_STORE_END;
}
static bool slotRead(const ConfigStore *store, uint32_t address, uint16_t size,
                     DeviceConfig *out, uint32_t *seq) {
  uint8_t bytes[RECORD_BYTES];
  return store->read(store->context, address, bytes, size) &&
         CONFIG_Decode(bytes, size, out, seq);
}
static bool later(uint32_t a, uint32_t b) { return (int32_t)(a-b)>0; }
// One candidate reused throughout: EEPROM growth must not multiply stack copies.
static bool loadPair(const ConfigStore *store, uint32_t base, uint16_t size,
                     DeviceConfig *config, uint32_t *sequence) {
  DeviceConfig candidate;
  uint32_t seq=0, best=0;
  bool found=false;
  for (unsigned i=0; i<CONFIG_STORE_SLOTS; ++i) {
    if (slotRead(store, base+i*size, size, &candidate, &seq) &&
        (!found || later(seq,best))) {
      *config=candidate; best=seq; found=true;
    }
  }
  if (found && sequence) *sequence=best;
  return found;
}
bool CONFIG_Load(const ConfigStore *store, DeviceConfig *config, uint32_t *sequence) {
  if (!ready(store) || !config) return false;
  if (loadPair(store, CONFIG_STORE_BASE, RECORD_BYTES, config, sequence)) return true;
  return loadPair(store, 0, CONFIG_LEGACY_SLOT_SIZE, config, sequence);
}
bool CONFIG_Save(const ConfigStore *store, const DeviceConfig *config,
                 uint32_t *sequence) {
  if (!ready(store) || !store->write || !config) return false;
  DeviceConfig candidate;
  uint32_t seq=0, best=0;
  unsigned newest=0;
  bool found=false;
  for (unsigned i=0; i<CONFIG_STORE_SLOTS; ++i) {
    if (slotRead(store, CONFIG_STORE_BASE+i*RECORD_BYTES, RECORD_BYTES, &candidate, &seq) &&
        (!found || later(seq,best))) {
      newest=i; best=seq; found=true;
    }
  }
  unsigned target=found ? newest^1u : 0;
  if (!found) {
    // Start beyond the legacy sequence, keeping both old 512-byte slots intact.
    for (unsigned i=0; i<CONFIG_STORE_SLOTS; ++i)
      if (slotRead(store, i*CONFIG_LEGACY_SLOT_SIZE, CONFIG_LEGACY_SLOT_SIZE, &candidate, &seq) &&
          (!found || later(seq,best))) { best=seq; found=true; }
  }
  uint32_t next=found ? best+1u : 1u;
  uint8_t bytes[RECORD_BYTES];
  uint32_t address=CONFIG_STORE_BASE+target*RECORD_BYTES;
  if (!CONFIG_Encode(config,next,bytes) ||
      !store->write(store->context,address,bytes,RECORD_BYTES) ||
      !slotRead(store,address,RECORD_BYTES,&candidate,&seq) || seq!=next) return false;
  if (sequence) *sequence=next;
  return true;
}
