#include "core/fields.h"
#include <string.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Fields are copied as little-endian bytes"
#endif

uint8_t *FIELDS_Code(uint8_t *p, void *object, const Field *f, uint8_t count, bool decode) {
  for (; count--; p += f++->width) {
    uint8_t *field = (uint8_t *)object + f->at;
    if (decode) { memset(field, 0, f->size); memcpy(field, p, f->width); }
    else memcpy(p, field, f->width);
  }
  return p;
}
