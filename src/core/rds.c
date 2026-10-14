#include "core/rds.h"
#include <string.h>

void RDS_Reset(RdsDecoder *decoder) { memset(decoder, 0, sizeof(*decoder)); }

static void textReset(RdsDecoder *d) {
  d->text[0] = 0;
  d->text_seen = d->text_confirmed = 0;
  memset(d->text_pending, 0, sizeof(d->text_pending));
}

static void displayText(char *out, const uint8_t *bytes, unsigned length) {
  for (unsigned i=0; i<length; ++i) {
    uint8_t c = bytes[i];
    out[i] = c >= 0x20 && c <= 0x7e ? (char)c : c < 0x20 ? ' ' : '?';
  }
  while (length && out[length-1] == ' ') --length;
  out[length] = 0;
}

static void publishText(RdsDecoder *d, unsigned segmentSize) {
  unsigned length = segmentSize*16;
  for (unsigned i=0; i<length; ++i) {
    if (!(d->text_confirmed & (1u << (i/segmentSize)))) return;
    if (d->text_pending[i] == 0x0d) {
      displayText(d->text, d->text_pending, i);
      return;
    }
  }
  displayText(d->text, d->text_pending, length);
}

bool RDS_Feed(RdsDecoder *d, const RdsGroup *g) {
  if (!d || !g || g->errors[0] > 1 || g->errors[1] > 1) return false;
  char previousPs[9], previousText[65];
  memcpy(previousPs, d->ps, sizeof(previousPs));
  memcpy(previousText, d->text, sizeof(previousText));
  if (!d->pi_valid || d->pi != g->blocks[0]) {
    RDS_Reset(d);
    d->pi = g->blocks[0];
    d->pi_valid = true;
  }
  uint16_t b = g->blocks[1];
  unsigned type = b >> 12, version = (b >> 11) & 1;
  if (type == 0 && g->errors[3] <= 1) {
    unsigned segment = b & 3;
    uint8_t bit = 1u << segment;
    uint8_t bytes[] = {g->blocks[3] >> 8, g->blocks[3] & 0xff};
    bool same = !memcmp(d->ps_pending+segment*2, bytes, 2);
    if (!same && d->ps_confirmed == 0x0f) {
      // Dynamic PS: collect a new complete name, retaining the last one.
      d->ps_seen = d->ps_confirmed = 0;
    }
    if (same && (d->ps_seen & bit)) d->ps_confirmed |= bit;
    else d->ps_confirmed &= ~bit;
    memcpy(d->ps_pending+segment*2, bytes, 2);
    d->ps_seen |= bit;
    if (d->ps_confirmed == 0x0f) displayText(d->ps, d->ps_pending, 8);
  } else if (type == 2 && g->errors[3] <= 1 &&
             (version || g->errors[2] <= 1)) {
    uint8_t ab = (b >> 4) & 1;
    if (!d->text_started || d->text_ab != ab || d->text_version != version) {
      textReset(d);
      d->text_started = true;
      d->text_ab = ab;
      d->text_version = version;
    }
    unsigned size = version ? 2 : 4, segment = b & 15;
    uint16_t bit = 1u << segment;
    uint8_t bytes[4] = {g->blocks[2] >> 8, g->blocks[2] & 0xff,
                        g->blocks[3] >> 8, g->blocks[3] & 0xff};
    const uint8_t *data = version ? bytes+2 : bytes;
    bool same = !memcmp(d->text_pending+segment*size, data, size);
    if (!same && d->text[0]) textReset(d); // Also tolerate missing A/B flips.
    if (same && (d->text_seen & bit)) d->text_confirmed |= bit;
    else d->text_confirmed &= ~bit;
    memcpy(d->text_pending+segment*size, data, size);
    d->text_seen |= bit;
    publishText(d, size);
  } else if (type == 4 && !version && !g->errors[2] && !g->errors[3]) {
    // MJD (17 bits), UTC hour and minute, local offset in half hours.
    uint32_t mjd = (uint32_t)(b & 3) << 15 | g->blocks[2] >> 1;
    unsigned hour = (g->blocks[2] & 1) << 4 | g->blocks[3] >> 12;
    unsigned minute = (g->blocks[3] >> 6) & 63, offset = (g->blocks[3] & 31) * 30;
    if (mjd && hour < 24 && minute < 60 && offset <= 14 * 60) {
      // Unsigned only: a signed % would pull in the division library.
      unsigned local = hour * 60 + minute + 1440;
      uint16_t now = (uint16_t)((g->blocks[3] & 32 ? local - offset : local + offset) % 1440);
      uint16_t last = d->clock_pending - 1;
      if (d->clock_pending && (now == last || now == (last + 1u) % 1440u)) {
        d->clock = now;
        d->clock_new = true;
      }
      d->clock_pending = now + 1;
    }
  }
  return strcmp(previousPs, d->ps) || strcmp(previousText, d->text);
}
