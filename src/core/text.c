#include "core/text.h"

Text TEXT_Begin(char *buffer, size_t size) {
  *buffer = 0;
  return (Text){buffer, buffer + size - 1};
}

void TEXT_Char(Text *text, char value) {
  if (text->at < text->end) *text->at++ = value;
  *text->at = 0;
}

void TEXT_Add(Text *text, const char *value) {
  if (value) while (*value) TEXT_Char(text, *value++);
}

void TEXT_Copy(char *buffer, size_t size, const char *value) {
  Text text = TEXT_Begin(buffer, size);
  TEXT_Add(&text, value);
}

void TEXT_Number(Text *text, uint32_t value, uint8_t width, uint8_t base) {
  char digits[11];
  uint8_t n = 0;
  do {
    digits[n++] = '0' + value % base;
    value /= base;
  } while (value);
  while (n < width && n < sizeof(digits)) digits[n++] = '0';
  while (n) TEXT_Char(text, digits[--n]);
}
