#include "utf8.h"

#define UTF8_CONTINUATION_MASK  0xC0
#define UTF8_CONTINUATION       0x80
#define UTF8_2_MASK             0xE0
#define UTF8_2                  0xC0
#define UTF8_3_MASK             0xF0
#define UTF8_3                  0xE0
#define UTF8_4_MASK             0xF8
#define UTF8_4                  0xF0

uint8_t utf8_is_continuation(uint8_t byte){
  return (byte & UTF8_CONTINUATION_MASK) == UTF8_CONTINUATION;
}

// bytes of the char by the first byte, 0 - the byte can not be the first
static uint8_t char_bytes(uint8_t first){
  if(first < 0x80) return 1;
  if((first & UTF8_2_MASK) == UTF8_2) return 2;
  if((first & UTF8_3_MASK) == UTF8_3) return 3;
  if((first & UTF8_4_MASK) == UTF8_4) return 4;
  return 0;
}

// the char at text (size bytes are there): code gets it, returns its bytes.
// a wrong byte is one char UTF8_INVALID
uint8_t utf8_decode(const uint8_t *text, uint16_t size, uint16_t *code){
  uint8_t bytes = char_bytes(text[0]);
  *code = UTF8_INVALID;
  if(!bytes || bytes > size) return 1;
  for(uint8_t i = 1; i < bytes; i++){
    if(!utf8_is_continuation(text[i])) return 1;
  }
  switch(bytes){
    case 1:
      *code = text[0];
      break;
    case 2:
      *code = ((uint16_t)(text[0] & 0x1F) << 6) | (text[1] & 0x3F);
      break;
    case 3:
      *code = ((uint16_t)(text[0] & 0x0F) << 12) | ((uint16_t)(text[1] & 0x3F) << 6) | (text[2] & 0x3F);
      break;
  }
  return bytes;
}

// bytes of the char to bytes (up to UTF8_MAX_BYTES), returns their number
uint8_t utf8_encode(uint16_t code, uint8_t *bytes){
  if(code < 0x80){
    bytes[0] = code;
    return 1;
  }
  if(code < 0x800){
    bytes[0] = UTF8_2 | (code >> 6);
    bytes[1] = UTF8_CONTINUATION | (code & 0x3F);
    return 2;
  }
  bytes[0] = UTF8_3 | (code >> 12);
  bytes[1] = UTF8_CONTINUATION | ((code >> 6) & 0x3F);
  bytes[2] = UTF8_CONTINUATION | (code & 0x3F);
  return 3;
}

uint8_t utf8_check(uint8_t *state, uint8_t byte){
  // state: continuation bytes to come
  if(*state){
    if(!utf8_is_continuation(byte)) return 0;
    (*state)--;
    return 1;
  }
  uint8_t bytes = char_bytes(byte);
  if(!bytes) return 0;
  *state = bytes - 1;
  return 1;
}
