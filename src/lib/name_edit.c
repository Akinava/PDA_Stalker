#include <string.h>
#include <util/delay.h>
#include "name_edit.h"
#include "display.h"
#include "keys.h"

// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20

// chars of short (8.3) names, space is the end of name
#define NAME_CHARS " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-~!#$%&'()@^{}"
// "[NAME    .EXT]": name from column 1, ext from column 10
#define NAME_FIELD_COL 1

static uint8_t wait_key(void){
  uint8_t key;
  while((key = keys_get_press()) == NOOP){
    _delay_ms(POLL_INTERVAL_MS);
  }
  return key;
}

static void draw_name_field(const uint8_t *raw, uint8_t pos){
  char line[DISPLAY_COLS + 1];
  char *p = line;
  *p++ = '[';
  memcpy(p, raw, FAT16_NAME_SIZE);
  p += FAT16_NAME_SIZE;
  *p++ = '.';
  memcpy(p, raw + FAT16_NAME_SIZE, FAT16_EXT_SIZE);
  p += FAT16_EXT_SIZE;
  *p++ = ']';
  *p = '\0';
  display_print_line(1, line);

  // '^' under the char, the dot is skipped
  uint8_t col = NAME_FIELD_COL + pos + (pos >= FAT16_NAME_SIZE);
  memset(line, ' ', col);
  line[col] = '^';
  line[col + 1] = '\0';
  display_print_line(2, line);
}

// remove spaces inside name and ext: "A B" -> "AB "
static void pack_name(uint8_t *part, uint8_t size){
  uint8_t len = 0;
  for(uint8_t i = 0; i < size; i++){
    if(part[i] != ' ') part[len++] = part[i];
  }
  memset(part + len, ' ', size - len);
}

uint8_t name_edit(const char *title, uint8_t *raw){
  static const char chars[] PROGMEM = NAME_CHARS;
  uint8_t pos = 0;

  display_print_line_P(0, title);
  display_print_line_P(3, PSTR("A-ok C-cancel"));

  while(1){
    draw_name_field(raw, pos);

    const char *c = strchr_P(chars, raw[pos]);
    uint8_t index = c ? c - chars : 0;
    uint8_t key = wait_key();
    if(key == C_KEY_PRESSED) return 0;
    if(key == A_KEY_PRESSED) break;
    switch(key){
      case UP_KEY_PRESSED:
        index = index < sizeof(chars) - 2 ? index + 1 : 0;
        raw[pos] = pgm_read_byte(&chars[index]);
        break;
      case DOWN_KEY_PRESSED:
        index = index ? index - 1 : sizeof(chars) - 2;
        raw[pos] = pgm_read_byte(&chars[index]);
        break;
      case LEFT_KEY_PRESSED:
        if(pos) pos--;
        break;
      case RIGHT_KEY_PRESSED:
        if(pos < FAT16_RAW_NAME_SIZE - 1) pos++;
        break;
    }
  }

  pack_name(raw, FAT16_NAME_SIZE);
  pack_name(raw + FAT16_NAME_SIZE, FAT16_EXT_SIZE);
  if(raw[0] == ' '){
    display_print_screen_P(PSTR("empty name"), PSTR(""), PSTR(""), PSTR("C - back"));
    while(wait_key() != C_KEY_PRESSED);
    return 0;
  }
  return 1;
}
