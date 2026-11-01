#ifndef CORE_FIELDS_H
#define CORE_FIELDS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A stored field: offset and size in its object, then its stored width,
// the field's low bytes in little-endian order. Decoding zero-extends into
// the whole field. Storage records and the PC link use these tables.
typedef struct { uint16_t at; uint8_t size, width; } Field;
#define FIELD(type, member, width) {offsetof(type, member), sizeof(((type *)0)->member), width}

// Encodes object's fields to p, or decodes them from p; returns the end.
uint8_t *FIELDS_Code(uint8_t *p, void *object, const Field *fields, uint8_t count, bool decode);
#define FIELDS_CODE(p, object, table, decode) \
  FIELDS_Code(p, object, table, sizeof(table) / sizeof(table[0]), decode)
#endif
