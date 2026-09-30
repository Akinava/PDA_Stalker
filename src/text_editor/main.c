#include <string.h>
#include <stdlib.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "display.h"
#include "display_gfx.h"
#include "keys.h"
#include "radio.h"
#include "sd.h"
#include "fat16_dir.h"
#include "fat16_edit.h"
#include "file_browser.h"
#include "name_edit.h"

// Editor of text and binary (hex) files on SD card.
// The whole file is in RAM, so its size is limited by EDIT_SIZE.
//
// navigation: arrows - cursor, A - change char, B - insert char, C - menu
// char change: UP / DOWN - char, LEFT / RIGHT - group of chars (text) or
//              nibble (hex), A - ok and next (text), C - ok, B - delete

// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20
// size of the file buffer
#define EDIT_SIZE 800

#define TEXT_ROWS (DISPLAY_GFX_ROWS - 1)
#define STATUS_ROW TEXT_ROWS
#define COLS DISPLAY_GFX_COLS
// chars of text line: the last column is for '\n' or the cursor at the end
#define TEXT_WIDTH (COLS - 1)

// hex row: "0000 00 11 22 33 abcd"
#define HEX_ROW_BYTES 4
#define HEX_OFFSET_DIGITS 4
#define HEX_BYTES_COL 5
#define HEX_BYTE_WIDTH 3
#define HEX_ASCII_COL 17

// status row: "T NAME.EXT*       123"
#define STATUS_NAME_COL 2
#define STATUS_POS_DIGITS 4

#define MODE_TEXT 0
#define MODE_HEX 1

#define MENU_SAVE    0
#define MENU_SAVE_AS 1
#define MENU_NEW     2
#define MENU_OPEN    3
#define MENU_MODE    4
#define MENU_EXIT    5
#define MENU_ITEMS   6
#define MENU_NONE    0xFF

// chars for text: LEFT / RIGHT - group, UP / DOWN - char in group
static const char group_upper[] PROGMEM = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char group_lower[] PROGMEM = "abcdefghijklmnopqrstuvwxyz";
static const char group_digit[] PROGMEM = "0123456789";
static const char group_other[] PROGMEM = " \n\t.,:;!?-+*/=()[]{}<>'\"#$%&@^_`|~\\";
static const char *const groups[] PROGMEM = {group_upper, group_lower, group_digit, group_other};
#define GROUPS (sizeof(groups) / sizeof(groups[0]))

static uint8_t sector[SD_SECTOR_SIZE];
static uint8_t data[EDIT_SIZE];
static uint16_t len;
static uint16_t cursor;           // byte 0..len, len - the end
static uint8_t nibble;            // hex: 0 - high, 1 - low
static uint8_t mode = MODE_TEXT;
static uint8_t editing;           // the char / nibble is being changed
static uint8_t modified;
static uint16_t top;              // the first line (text) or row (hex) on the screen
static uint8_t last_char = 'a';   // inserted char
static fat16_entry_t file;
static uint8_t has_file;
static const char *status_message; // in flash, shown instead of the name once

// rows on the screen: only changed rows are sent to the display
static uint16_t row_hash[DISPLAY_GFX_ROWS];
static uint8_t rows_valid;

/****************************** text layout **********************************/

// a line is ended by '\n' (it is the last char of the line) or by TEXT_WIDTH
// chars, returns the start of the next line
static uint16_t line_end(uint16_t start){
  uint8_t col = 0;
  while(start < len){
    if(data[start] == '\n') return start + 1;
    if(col == TEXT_WIDTH) break;
    start++;
    col++;
  }
  return start;
}

// the last line can hold the end position (cursor after the last char)
static uint8_t line_open(uint16_t start, uint16_t end){
  return end == len && !(end > start && data[end - 1] == '\n');
}

// line and column of the position
static void text_locate(uint16_t pos, uint16_t *line, uint8_t *col){
  uint16_t start = 0;
  uint16_t n = 0;
  while(1){
    uint16_t end = line_end(start);
    if(pos < end || (pos == len && line_open(start, end))){
      *line = n;
      *col = pos - start;
      return;
    }
    // the end position on the empty line after '\n' or the full line
    if(end >= len){
      *line = n + 1;
      *col = 0;
      return;
    }
    start = end;
    n++;
  }
}

// start of the line, len for the empty line at the end
static uint16_t line_start(uint16_t n){
  uint16_t start = 0;
  while(n--){
    uint16_t end = line_end(start);
    if(end >= len) return len;
    start = end;
  }
  return start;
}

// position at column of the line, the column is clamped to the line
static uint16_t text_pos(uint16_t n, uint8_t col){
  uint16_t last;
  uint8_t c;
  text_locate(len, &last, &c);
  if(n > last) n = last;

  uint16_t start = line_start(n);
  if(start >= len) return len;
  uint16_t end = line_end(start);
  uint16_t max = line_open(start, end) ? end : end - 1;
  return start + col > max ? max : start + col;
}

/******************************** screen *************************************/

static uint8_t glyph(uint8_t c, uint8_t at_cursor){
  if(c == '\n') return at_cursor ? DISPLAY_GFX_NEWLINE : ' ';
  if(c == '\t') return DISPLAY_GFX_TAB;
  if(c == '\r') return DISPLAY_GFX_CR;
  if(c < ' ' || c > '~') return DISPLAY_GFX_UNKNOWN;
  return c;
}

static char hex_digit(uint8_t value){
  return value < 10 ? '0' + value : 'A' + value - 10;
}

// send the row only if it is changed
static void draw_row(uint8_t row, const char *text, uint8_t cursor_col, uint8_t invert){
  uint16_t hash = cursor_col * 31 + invert;
  for(uint8_t i = 0; i < COLS; i++){
    hash = hash * 31 + (uint8_t)text[i];
  }
  if((rows_valid & (1 << row)) && row_hash[row] == hash) return;
  display_gfx_row(row, text, COLS, cursor_col, invert);
  row_hash[row] = hash;
  rows_valid |= 1 << row;
}

static void draw_text(void){
  uint16_t cursor_line;
  uint8_t cursor_col;
  uint16_t last;
  uint8_t c;

  text_locate(cursor, &cursor_line, &cursor_col);
  text_locate(len, &last, &c);
  if(cursor_line < top) top = cursor_line;
  if(cursor_line >= top + TEXT_ROWS) top = cursor_line - TEXT_ROWS + 1;

  uint16_t start = line_start(top);
  for(uint8_t row = 0; row < TEXT_ROWS; row++){
    char text[COLS];
    uint16_t n = top + row;
    memset(text, ' ', COLS);
    if(n <= last && start < len){
      uint16_t end = line_end(start);
      for(uint16_t p = start; p < end; p++){
        text[p - start] = glyph(data[p], p == cursor);
      }
      start = end;
    }
    draw_row(row, text, n == cursor_line ? cursor_col : DISPLAY_GFX_NO_CURSOR, 0);
  }
}

static void draw_hex(void){
  uint16_t cursor_row = cursor / HEX_ROW_BYTES;
  if(cursor_row < top) top = cursor_row;
  if(cursor_row >= top + TEXT_ROWS) top = cursor_row - TEXT_ROWS + 1;

  for(uint8_t row = 0; row < TEXT_ROWS; row++){
    char text[COLS];
    uint16_t offset = (top + row) * HEX_ROW_BYTES;
    memset(text, ' ', COLS);
    // rows up to the one with the end position
    if(offset <= len){
      for(uint8_t i = 0; i < HEX_OFFSET_DIGITS; i++){
        text[i] = hex_digit((offset >> ((HEX_OFFSET_DIGITS - 1 - i) * 4)) & 0x0F);
      }
      for(uint8_t i = 0; i < HEX_ROW_BYTES && offset + i < len; i++){
        uint8_t value = data[offset + i];
        text[HEX_BYTES_COL + i * HEX_BYTE_WIDTH] = hex_digit(value >> 4);
        text[HEX_BYTES_COL + i * HEX_BYTE_WIDTH + 1] = hex_digit(value & 0x0F);
        text[HEX_ASCII_COL + i] = value >= ' ' && value <= '~' ? value : '.';
      }
    }
    uint8_t col = DISPLAY_GFX_NO_CURSOR;
    if(top + row == cursor_row){
      col = HEX_BYTES_COL + (cursor % HEX_ROW_BYTES) * HEX_BYTE_WIDTH + nibble;
    }
    draw_row(row, text, col, 0);
  }
}

// "T NAME.EXT*      123": mode (T, H, E - editing), name, position
static void draw_status(void){
  char text[COLS];
  memset(text, ' ', COLS);
  text[0] = editing ? 'E' : mode == MODE_HEX ? 'H' : 'T';

  char *p = text + STATUS_NAME_COL;
  if(status_message){
    strcpy_P(p, status_message);
    p += strlen(p);
    status_message = NULL;
  }else{
    if(has_file){
      strcpy(p, file.name);
    }else{
      strcpy_P(p, PSTR("new"));
    }
    p += strlen(p);
    if(modified) *p++ = '*';
  }
  *p = ' ';

  char pos[6];
  utoa(cursor, pos, 10);
  uint8_t digits = strlen(pos);
  memcpy(text + COLS - digits, pos, digits);
  draw_row(STATUS_ROW, text, DISPLAY_GFX_NO_CURSOR, 1);
}

static void draw_editor(void){
  if(mode == MODE_HEX){
    draw_hex();
  }else{
    draw_text();
  }
  draw_status();
}

/********************************* edit **************************************/

static uint8_t insert_byte(uint16_t pos, uint8_t value){
  if(len == EDIT_SIZE){
    status_message = PSTR("buffer is full");
    return 0;
  }
  memmove(data + pos + 1, data + pos, len - pos);
  data[pos] = value;
  len++;
  modified = 1;
  return 1;
}

static void delete_byte(uint16_t pos){
  if(pos >= len) return;
  memmove(data + pos, data + pos + 1, len - pos - 1);
  len--;
  modified = 1;
}

// group of the char and its index in the group, 0 if the char is in no group
static uint8_t find_char(uint8_t c, uint8_t *group, uint8_t *index){
  for(uint8_t g = 0; g < GROUPS; g++){
    const char *chars = (const char *)pgm_read_word(&groups[g]);
    const char *found = strchr_P(chars, c);
    if(c && found){
      *group = g;
      *index = found - chars;
      return 1;
    }
  }
  return 0;
}

static void change_char(uint8_t key){
  uint8_t group = 0;
  uint8_t index = 0;
  uint8_t found = find_char(data[cursor], &group, &index);
  if(found){
    const char *chars = (const char *)pgm_read_word(&groups[group]);
    uint8_t size = strlen_P(chars);
    switch(key){
      case UP_KEY_PRESSED:
        index = index + 1 < size ? index + 1 : 0;
        break;
      case DOWN_KEY_PRESSED:
        index = index ? index - 1 : size - 1;
        break;
      case LEFT_KEY_PRESSED:
        group = group ? group - 1 : GROUPS - 1;
        break;
      case RIGHT_KEY_PRESSED:
        group = group + 1 < GROUPS ? group + 1 : 0;
        break;
    }
  }
  // the same place in the new group: 'c' -> 'C'
  const char *chars = (const char *)pgm_read_word(&groups[group]);
  uint8_t size = strlen_P(chars);
  if(index >= size) index = size - 1;
  data[cursor] = pgm_read_byte(&chars[index]);
  modified = 1;
}

static void change_nibble(uint8_t key){
  uint8_t shift = nibble ? 0 : 4;
  uint8_t value = (data[cursor] >> shift) & 0x0F;
  value = (key == UP_KEY_PRESSED ? value + 1 : value - 1) & 0x0F;
  data[cursor] = (data[cursor] & ~(0x0F << shift)) | (value << shift);
  modified = 1;
}

static void text_key(uint8_t key){
  uint16_t line;
  uint8_t col;

  if(editing){
    switch(key){
      case A_KEY_PRESSED:
        last_char = data[cursor];
        editing = 0;
        cursor++;
        break;
      case C_KEY_PRESSED:
        last_char = data[cursor];
        editing = 0;
        break;
      case B_KEY_PRESSED:
        delete_byte(cursor);
        editing = 0;
        break;
      default:
        change_char(key);
    }
    return;
  }

  switch(key){
    case LEFT_KEY_PRESSED:
      if(cursor) cursor--;
      break;
    case RIGHT_KEY_PRESSED:
      if(cursor < len) cursor++;
      break;
    case UP_KEY_PRESSED:
      text_locate(cursor, &line, &col);
      if(line) cursor = text_pos(line - 1, col);
      break;
    case DOWN_KEY_PRESSED:
      text_locate(cursor, &line, &col);
      cursor = text_pos(line + 1, col);
      break;
    case A_KEY_PRESSED:
      // at the end: a new char
      if(cursor < len || insert_byte(cursor, last_char)) editing = 1;
      break;
    case B_KEY_PRESSED:
      if(insert_byte(cursor, last_char)) editing = 1;
      break;
  }
}

static void hex_key(uint8_t key){
  if(editing){
    switch(key){
      case UP_KEY_PRESSED:
      case DOWN_KEY_PRESSED:
        change_nibble(key);
        break;
      case LEFT_KEY_PRESSED:
        if(nibble){
          nibble = 0;
        }else if(cursor){
          cursor--;
          nibble = 1;
        }
        break;
      case RIGHT_KEY_PRESSED:
        // only on the existing bytes
        if(!nibble){
          nibble = 1;
        }else if(cursor + 1 < len){
          cursor++;
          nibble = 0;
        }
        break;
      case B_KEY_PRESSED:
        delete_byte(cursor);
        nibble = 0;
        editing = 0;
        break;
      default:
        editing = 0;
    }
    return;
  }

  switch(key){
    case LEFT_KEY_PRESSED:
      if(nibble){
        nibble = 0;
      }else if(cursor){
        cursor--;
        nibble = 1;
      }
      break;
    case RIGHT_KEY_PRESSED:
      if(cursor == len) break;
      if(!nibble){
        nibble = 1;
      }else{
        cursor++;
        nibble = 0;
      }
      break;
    case UP_KEY_PRESSED:
      if(cursor >= HEX_ROW_BYTES) cursor -= HEX_ROW_BYTES;
      break;
    case DOWN_KEY_PRESSED:
      cursor = cursor + HEX_ROW_BYTES < len ? cursor + HEX_ROW_BYTES : len;
      break;
    case A_KEY_PRESSED:
      if(cursor < len || insert_byte(cursor, 0)) editing = 1;
      break;
    case B_KEY_PRESSED:
      if(insert_byte(cursor, 0)){
        nibble = 0;
        editing = 1;
      }
      break;
  }
  if(cursor == len) nibble = 0;
}

/****************************** text screens *********************************/

// SD traffic corrupts display RAM, so every screen is drawn completely
// after the SD work: lines are in flash, NULL line is printed from RAM
#define draw_screen display_print_screen_P

static uint8_t wait_key(void){
  uint8_t key;
  while((key = keys_get_press()) == NOOP){
    _delay_ms(POLL_INTERVAL_MS);
  }
  return key;
}

// keys of the editor and of the text screens must not see the same press
static void wait_release(void){
  while(keys_read() != NOOP){
    _delay_ms(POLL_INTERVAL_MS);
  }
  keys_get_press();
  keys_get_repeat(POLL_INTERVAL_MS);
}

// l0 is in flash, name is in RAM (may be NULL) on line 1
static void message(const char *l0, const char *name){
  draw_screen(l0, name ? NULL : PSTR(""), PSTR(""), PSTR("C - back"));
  if(name) display_print_line(1, name);
  while(wait_key() != C_KEY_PRESSED);
}

// l0 is in flash, name is in RAM (may be NULL) on line 1, returns 1 on A
static uint8_t sure(const char *l0, const char *name){
  draw_screen(l0, name ? NULL : PSTR(""), PSTR("A - yes"), PSTR("C - no"));
  if(name) display_print_line(1, name);
  while(1){
    switch(wait_key()){
      case A_KEY_PRESSED: return 1;
      case C_KEY_PRESSED: return 0;
    }
  }
}

static void show_error(uint8_t error){
  static const char texts[][17] PROGMEM = {
    [FAT16_OK]             = "",
    [FAT16_ERROR_IO]       = "SD card error",
    [FAT16_ERROR_FULL]     = "card is full",
    [FAT16_ERROR_DIR_FULL] = "root dir is full",
    [FAT16_ERROR_EXISTS]   = "name exists",
    [FAT16_ERROR_SAME_DIR] = "same directory",
    [FAT16_ERROR_INSIDE]   = "into itself",
    [FAT16_ERROR_DEPTH]    = "too deep dirs",
  };
  if(error) message(texts[error], NULL);
}

// unsaved changes are lost: ask first
static uint8_t can_discard(void){
  return !modified || sure(PSTR("discard changes?"), NULL);
}

// init card and FAT16, retry until success
static void mount(void){
  while(1){
    draw_screen(PSTR("SD init..."), PSTR(""), PSTR(""), PSTR(""));
    if(sd_init_card() == SD_TYPE_NONE){
      draw_screen(PSTR("SD init error"), PSTR("insert the card"), PSTR(""), PSTR("A - retry"));
    }else if(!fat16_mount(sector)){
      draw_screen(PSTR("no FAT16"), PSTR("on the card"), PSTR(""), PSTR("A - retry"));
    }else{
      return;
    }
    while(wait_key() != A_KEY_PRESSED);
  }
}

/********************************* files *************************************/

static uint8_t is_binary(void){
  for(uint16_t i = 0; i < len; i++){
    uint8_t c = data[i];
    if(c > '~' || (c < ' ' && c != '\n' && c != '\r' && c != '\t')) return 1;
  }
  return 0;
}

static void clear_buffer(void){
  len = 0;
  cursor = 0;
  nibble = 0;
  top = 0;
  modified = 0;
  editing = 0;
}

static void new_file(void){
  if(!can_discard()) return;
  clear_buffer();
  mode = MODE_TEXT;
  has_file = 0;
}

static void open_file(void){
  fat16_entry_t entry;
  fat16_file_t reader;
  uint16_t read;

  if(!can_discard()) return;
  file_browser_open(sector);
  // B is not used, C in the root directory - cancel
  while(1){
    uint8_t result = file_browser_run(&entry);
    if(result == FILE_BROWSER_EXIT) return;
    if(result == FILE_BROWSER_FILE) break;
  }
  if(entry.size > EDIT_SIZE){
    message(PSTR("file too big"), entry.name);
    return;
  }

  draw_screen(PSTR("reading..."), NULL, PSTR(""), PSTR(""));
  display_print_line(1, entry.name);
  clear_buffer();
  has_file = 0;
  fat16_file_open(&reader, entry.cluster, entry.size);
  do{
    if(!fat16_file_read(&reader, sector, &read)){
      message(PSTR("SD read error"), entry.name);
      clear_buffer();
      return;
    }
    memcpy(data + len, sector, read);
    len += read;
  }while(read);

  file = entry;
  has_file = 1;
  mode = is_binary() ? MODE_HEX : MODE_TEXT;
}

// A on a file - write to it, B - a new file in the directory, C - cancel
static void save_as(void){
  fat16_entry_t entry;
  uint8_t raw[FAT16_RAW_NAME_SIZE];

  draw_screen(PSTR("save as:"), PSTR("A - to the file"), PSTR("B - new file"), PSTR("C - cancel"));
  if(wait_key() == C_KEY_PRESSED) return;

  file_browser_open(sector);
  while(1){
    uint8_t result = file_browser_run(&entry);
    if(result == FILE_BROWSER_EXIT) return;

    if(result == FILE_BROWSER_FILE){
      if(!sure(PSTR("replace file?"), entry.name)) continue;
      draw_screen(PSTR("saving..."), NULL, PSTR(""), PSTR(""));
      display_print_line(1, entry.name);
      uint8_t error = fat16_write_file(&entry, data, len, sector);
      if(error){
        show_error(error);
        continue;
      }
      break;
    }

    // B: the name of the new file, the current name at the start
    if(has_file){
      fat16_raw_name(&file, raw);
    }else{
      memset(raw, ' ', sizeof(raw));
    }
    if(!name_edit(PSTR("file name"), raw)) continue;
    draw_screen(PSTR("saving..."), PSTR(""), PSTR(""), PSTR(""));
    uint8_t error = fat16_create_file(file_browser_dir(), raw, data, len, &entry, sector);
    if(error){
      show_error(error);
      continue;
    }
    break;
  }
  file = entry;
  has_file = 1;
  modified = 0;
}

static void save(void){
  if(!has_file){
    save_as();
    return;
  }
  draw_screen(PSTR("saving..."), NULL, PSTR(""), PSTR(""));
  display_print_line(1, file.name);
  // the card could be changed or the file deleted
  if(!fat16_entry_valid(&file, sector)){
    message(PSTR("file not found"), file.name);
    return;
  }
  uint8_t error = fat16_write_file(&file, data, len, sector);
  show_error(error);
  if(!error) modified = 0;
}

/********************************** menu *************************************/

// returns the chosen item or MENU_NONE on C
static uint8_t menu(void){
  static const char items[MENU_ITEMS][10] PROGMEM = {
    "save", "save as", "new file", "open file", "", "exit"
  };
  uint8_t selected = 0;
  uint8_t first = 0;

  while(1){
    // keep selected item on the screen
    if(selected < first) first = selected;
    if(selected >= first + DISPLAY_ROWS) first = selected - DISPLAY_ROWS + 1;

    for(uint8_t row = 0; row < DISPLAY_ROWS; row++){
      char line[DISPLAY_COLS + 1];
      uint8_t item = first + row;
      line[0] = item == selected ? '>' : ' ';
      if(item == MENU_MODE){
        strcpy_P(line + 1, mode == MODE_HEX ? PSTR("text mode") : PSTR("hex mode"));
      }else{
        strcpy_P(line + 1, items[item]);
      }
      display_print_line(row, line);
    }

    switch(wait_key()){
      case UP_KEY_PRESSED:
        selected = selected ? selected - 1 : MENU_ITEMS - 1;
        break;
      case DOWN_KEY_PRESSED:
        selected = selected < MENU_ITEMS - 1 ? selected + 1 : 0;
        break;
      case A_KEY_PRESSED:
        return selected;
      case C_KEY_PRESSED:
        return MENU_NONE;
    }
  }
}

// menu and file operations are in the text mode of the display
static void run_menu(void){
  wait_release();
  display_gfx_off();

  switch(menu()){
    case MENU_SAVE:
      save();
      break;
    case MENU_SAVE_AS:
      save_as();
      break;
    case MENU_NEW:
      new_file();
      break;
    case MENU_OPEN:
      open_file();
      break;
    case MENU_MODE:
      mode = mode == MODE_HEX ? MODE_TEXT : MODE_HEX;
      nibble = 0;
      top = 0;
      break;
    case MENU_EXIT:
      if(can_discard()) menu_exit_app();
      break;
  }

  wait_release();
  display_gfx_on();
  rows_valid = 0;
}

/********************************** main *************************************/

int main(void){
  init_keys();
  // CSN high before any SPI traffic, otherwise radio catches display data
  init_radio();
  init_sd();
  init_display();

  mount();
  display_gfx_on();
  draw_editor();

  while(1){
    uint8_t key = keys_get_repeat(POLL_INTERVAL_MS);
    if(key != NOOP){
      if(key == C_KEY_PRESSED && !editing){
        run_menu();
      }else if(mode == MODE_HEX){
        hex_key(key);
      }else{
        text_key(key);
      }
      draw_editor();
    }
    _delay_ms(POLL_INTERVAL_MS);
  }
}
