#ifndef CORE_TEXT_H
#define CORE_TEXT_H
#include <stddef.h>
#include <stdint.h>

// Bounded text building instead of printf: every call keeps the result
// NUL-terminated and silently truncates at the buffer end.
typedef struct {
  char *at, *end; // end is the last byte, reserved for the terminator.
} Text;

Text TEXT_Begin(char *buffer, size_t size);
// Replaces buffer with value; the one-call form of Begin + Add.
void TEXT_Copy(char *buffer, size_t size, const char *value);
void TEXT_Add(Text *text, const char *value);
void TEXT_Char(Text *text, char value);
// Digits in base 8 or 10, zero-padded to at least width.
void TEXT_Number(Text *text, uint32_t value, uint8_t width, uint8_t base);
#endif
