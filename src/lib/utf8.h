#include <stdint.h>

#ifndef UTF8_H
#define UTF8_H

// UTF-8 of the basic plane (U+0000..U+FFFF), chars of 4 bytes and wrong
// bytes are UTF8_INVALID

#define UTF8_INVALID            0xFFFD
#define UTF8_MAX_BYTES          3

uint8_t utf8_is_continuation(uint8_t byte);
uint8_t utf8_decode(const uint8_t *text, uint16_t size, uint16_t *code);
uint8_t utf8_encode(uint16_t code, uint8_t *bytes);

// check of the text by parts: state is 0 at the start and at the end of
// a correct text, returns 0 if the byte is wrong
uint8_t utf8_check(uint8_t *state, uint8_t byte);

#endif
