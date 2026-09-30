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
#include "loader.h"

// Editor of text and binary (hex) files on SD card.
// The document is in the temp file on the card by pages, only the current
// page is in RAM, so the file size is not limited by RAM.
//
// navigation: arrows - cursor, A - change char, B - insert char, C - menu
// char change: UP / DOWN - char, LEFT / RIGHT - group of chars (text) or
//              nibble (hex), A - ok and next (text), C - ok, B - delete

// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20

// the temp file: one sector is one page, 2 bytes of the page length,
// then up to PAGE_DATA bytes of the document
#define PAGE_HEADER 2
#define PAGE_DATA (SD_SECTOR_SIZE - PAGE_HEADER)
// open: a page is cut after '\n' from PAGE_FILL_MIN bytes or at PAGE_FILL_MAX,
// the rest of the page is free for inserts
#define PAGE_FILL_MIN 256
#define PAGE_FILL_MAX 400
// split of the full page: after '\n' in the middle half, else in the middle
#define SPLIT_FROM (PAGE_DATA / 4)
#define SPLIT_TO (PAGE_DATA * 3 / 4)

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

// status row: "T NAME.EXT*    3/12"
#define STATUS_NAME_COL 2

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

// temp file "~TEXTED.TMP" in the root directory
static const uint8_t temp_name[FAT16_RAW_NAME_SIZE] PROGMEM = "~TEXTED TMP";

static uint8_t sector[SD_SECTOR_SIZE];
// the current page: length and data
static uint8_t page[SD_SECTOR_SIZE];
#define data (page + PAGE_HEADER)
static uint16_t len;              // of the current page
static uint16_t page_index;
static uint16_t pages;
static uint8_t page_dirty;        // the page is not written to the temp file
static uint32_t page_offset;      // of the current page in the document
static uint32_t total;            // length of the document

static fat16_entry_t temp;
static uint16_t temp_last;        // the last cluster of the temp file
static uint16_t temp_clusters;
static fat16_seek_t temp_seek;

static uint16_t cursor;           // in the page: 0..len, len - the end
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
// SD traffic corrupts the display RAM: the next draw is complete
static uint8_t sd_traffic;

// the start of the next page is shown after the current page
#define PREVIEW_SIZE (TEXT_ROWS * COLS)
static uint8_t preview[PREVIEW_SIZE];
static uint8_t preview_len;

/********************************** pages ************************************/

static uint16_t get16(const uint8_t *buf){
  return buf[0] | ((uint16_t)buf[1] << 8);
}

static void put16(uint8_t *buf, uint16_t value){
  buf[0] = value;
  buf[1] = value >> 8;
}

// card sector of the page, the temp file grows if needed
static uint8_t page_sector(uint16_t index, uint32_t *place){
  sd_traffic = 1;
  uint16_t need = index / fat16_volume.cluster_sectors + 1;
  while(temp_clusters < need){
    uint16_t cluster;
    if(fat16_chain_append(temp_last, &cluster, sector)) return 0;
    temp_last = cluster;
    temp_clusters++;
    // the record size is the chain size: the card stays consistent
    uint32_t size = (uint32_t)temp_clusters * fat16_volume.cluster_sectors * SD_SECTOR_SIZE;
    if(fat16_set_data(&temp, temp.cluster ? temp.cluster : cluster, size, NULL, sector)) return 0;
  }
  return fat16_file_sector(temp.cluster, index, &temp_seek, place, sector);
}

static uint8_t page_write(void){
  uint32_t place;
  if(!page_dirty) return 1;
  if(!page_sector(page_index, &place)) return 0;
  put16(page, len);
  if(!sd_write_sector(place, page)) return 0;
  page_dirty = 0;
  return 1;
}

static uint8_t page_read(uint16_t index){
  uint32_t place;
  if(!page_sector(index, &place) || !sd_read_sector(place, page)) return 0;
  len = get16(page);
  if(len > PAGE_DATA) len = PAGE_DATA;
  page_index = index;
  page_dirty = 0;
  return 1;
}

// the start of the next page for the screen (it does not change while
// the current page is edited)
static void preview_load(void){
  uint32_t place;
  preview_len = 0;
  if(page_index + 1 >= pages) return;
  if(!page_sector(page_index + 1, &place) || !sd_read_sector(place, sector)) return;
  uint16_t length = get16(sector);
  if(length > PAGE_DATA) length = 0;
  preview_len = length < PREVIEW_SIZE ? length : PREVIEW_SIZE;
  memcpy(preview, sector + PAGE_HEADER, preview_len);
}

// the next / previous page becomes the current one, returns 0 if there is none
static uint8_t page_next(void){
  if(page_index + 1 >= pages) return 0;
  uint32_t offset = page_offset + len;
  if(!page_write() || !page_read(page_index + 1)){
    status_message = PSTR("SD card error");
    return 0;
  }
  page_offset = offset;
  top = 0;
  preview_load();
  return 1;
}

static uint8_t page_prev(void){
  if(!page_index) return 0;
  if(!page_write() || !page_read(page_index - 1)){
    status_message = PSTR("SD card error");
    return 0;
  }
  page_offset -= len;
  top = 0;
  preview_load();
  return 1;
}

// copy through the sector buffer: sectors first, the FAT uses the buffer
static uint8_t page_copy(uint16_t from, uint16_t to){
  uint32_t src;
  uint32_t dst;
  return page_sector(to, &dst) && page_sector(from, &src)
      && sd_read_sector(src, sector) && sd_write_sector(dst, sector);
}

// a free page after the current one: the next pages go one page further
static uint8_t page_insert_after(void){
  for(uint16_t i = pages; i > page_index + 1; i--){
    if(!page_copy(i - 1, i)) return 0;
  }
  pages++;
  return 1;
}

// remove the current page: the next pages go one page back
static uint8_t page_remove(void){
  for(uint16_t i = page_index + 1; i < pages; i++){
    if(!page_copy(i, i - 1)) return 0;
  }
  pages--;
  return 1;
}

static void draw_status(void);

// the full page is split in two: after '\n' if it is near the middle
static uint8_t page_split(void){
  uint16_t cut = PAGE_DATA / 2;
  uint32_t place;

  for(uint16_t i = SPLIT_TO; i > SPLIT_FROM; i--){
    if(data[i - 1] == '\n'){
      cut = i;
      break;
    }
  }
  // the next pages are moved: it takes time
  status_message = PSTR("wait...");
  draw_status();

  if(!page_insert_after() || !page_sector(page_index + 1, &place)) return 0;
  put16(sector, len - cut);
  memcpy(sector + PAGE_HEADER, data + cut, len - cut);
  if(!sd_write_sector(place, sector)) return 0;
  len = cut;
  page_dirty = 1;

  // the cursor is in the second half: go to the new page
  if(cursor > cut){
    uint16_t pos = cursor - cut;
    if(!page_next()) return 0;
    cursor = pos;
  }else{
    preview_load();
  }
  return 1;
}

// a new empty document
static void document_clear(void){
  pages = 1;
  page_index = 0;
  len = 0;
  page_dirty = 1;
  page_offset = 0;
  total = 0;
  cursor = 0;
  nibble = 0;
  top = 0;
  editing = 0;
  modified = 0;
  preview_len = 0;
}

// the old temp file is deleted, the new one is empty
static uint8_t temp_create(void){
  uint8_t raw[FAT16_RAW_NAME_SIZE];
  memcpy_P(raw, temp_name, sizeof(raw));
  if(fat16_find(FAT16_ROOT_CLUSTER, raw, &temp, sector) && fat16_delete(&temp, sector)) return 0;
  if(fat16_create_file(FAT16_ROOT_CLUSTER, raw, NULL, 0, &temp, sector)) return 0;
  temp_last = 0;
  temp_clusters = 0;
  temp_seek.cluster = 0;
  return 1;
}

/****************************** text layout **********************************/

// a line is ended by '\n' (it is the last char of the line) or by TEXT_WIDTH
// chars, returns the start of the next line of the text of size bytes
static uint16_t text_line_end(const uint8_t *text, uint16_t size, uint16_t start){
  uint8_t col = 0;
  while(start < size){
    if(text[start] == '\n') return start + 1;
    if(col == TEXT_WIDTH) break;
    start++;
    col++;
  }
  return start;
}

// line end in the current page
static uint16_t line_end(uint16_t start){
  return text_line_end(data, len, start);
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

static uint16_t last_text_line(void);

static void draw_text(void){
  uint16_t cursor_line;
  uint8_t cursor_col;

  text_locate(cursor, &cursor_line, &cursor_col);
  // lines of the page on the screen, then the lines of the next page
  uint16_t last = last_text_line();
  if(cursor_line < top) top = cursor_line;
  if(cursor_line >= top + TEXT_ROWS) top = cursor_line - TEXT_ROWS + 1;

  uint16_t start = line_start(top);
  uint16_t next_start = 0;
  for(uint8_t row = 0; row < TEXT_ROWS; row++){
    char text[COLS];
    uint16_t n = top + row;
    memset(text, ' ', COLS);
    if(n <= last){
      if(start < len){
        uint16_t end = line_end(start);
        for(uint16_t p = start; p < end; p++){
          text[p - start] = glyph(data[p], p == cursor);
        }
        start = end;
      }
    }else if(next_start < preview_len){
      uint16_t end = text_line_end(preview, preview_len, next_start);
      for(uint16_t p = next_start; p < end; p++){
        text[p - next_start] = glyph(preview[p], 0);
      }
      next_start = end;
    }
    draw_row(row, text, n == cursor_line ? cursor_col : DISPLAY_GFX_NO_CURSOR, 0);
  }
}

static void draw_hex(void){
  uint16_t cursor_row = cursor / HEX_ROW_BYTES;
  if(cursor_row < top) top = cursor_row;
  if(cursor_row >= top + TEXT_ROWS) top = cursor_row - TEXT_ROWS + 1;

  // rows of the page (the last page has the row of the end position),
  // then the rows of the next page
  uint16_t page_rows = page_index + 1 < pages ? (len + HEX_ROW_BYTES - 1) / HEX_ROW_BYTES
                                              : len / HEX_ROW_BYTES + 1;
  for(uint8_t row = 0; row < TEXT_ROWS; row++){
    char text[COLS];
    uint16_t index = top + row;
    const uint8_t *bytes = data;
    uint16_t size = len;
    uint16_t offset = index * HEX_ROW_BYTES;
    uint32_t address = page_offset + offset;
    if(index >= page_rows){
      bytes = preview;
      size = preview_len;
      offset = (index - page_rows) * HEX_ROW_BYTES;
      address = page_offset + len + offset;
    }
    memset(text, ' ', COLS);
    if(index < page_rows || offset < size){
      // the lower digits of the offset in the document
      for(uint8_t i = 0; i < HEX_OFFSET_DIGITS; i++){
        text[i] = hex_digit((address >> ((HEX_OFFSET_DIGITS - 1 - i) * 4)) & 0x0F);
      }
      for(uint8_t i = 0; i < HEX_ROW_BYTES && offset + i < size; i++){
        uint8_t value = bytes[offset + i];
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

// "T NAME.EXT*    3/12": mode (T, H, E - editing), name, page / pages
static void draw_status(void){
  char text[COLS + 1];
  char right[12];

  memset(text, ' ', COLS);
  text[0] = editing ? 'E' : mode == MODE_HEX ? 'H' : 'T';

  utoa(page_index + 1, right, 10);
  strcat_P(right, PSTR("/"));
  utoa(pages, right + strlen(right), 10);
  uint8_t right_len = strlen(right);
  memcpy(text + COLS - right_len, right, right_len);

  // the name or the message up to the page number
  char name[COLS + 1];
  if(status_message){
    strcpy_P(name, status_message);
    status_message = NULL;
  }else{
    if(has_file){
      strcpy(name, file.name);
    }else{
      strcpy_P(name, PSTR("new"));
    }
    if(modified) strcat_P(name, PSTR("*"));
  }
  uint8_t room = COLS - STATUS_NAME_COL - right_len - 1;
  uint8_t name_len = strlen(name);
  memcpy(text + STATUS_NAME_COL, name, name_len < room ? name_len : room);
  draw_row(STATUS_ROW, text, DISPLAY_GFX_NO_CURSOR, 1);
}

static void draw_editor(void){
  if(sd_traffic){
    rows_valid = 0;
    sd_traffic = 0;
  }
  if(mode == MODE_HEX){
    draw_hex();
  }else{
    draw_text();
  }
  draw_status();
}

/********************************* edit **************************************/

// insert at the cursor, the full page is split
static uint8_t insert_byte(uint8_t value){
  if(len == PAGE_DATA && !page_split()){
    status_message = PSTR("SD card error");
    return 0;
  }
  memmove(data + cursor + 1, data + cursor, len - cursor);
  data[cursor] = value;
  len++;
  total++;
  modified = 1;
  page_dirty = 1;
  return 1;
}

// delete at the cursor, the empty page is removed
static void delete_byte(void){
  if(cursor >= len) return;
  memmove(data + cursor, data + cursor + 1, len - cursor - 1);
  len--;
  total--;
  modified = 1;
  page_dirty = 1;
  if(len || pages == 1) return;

  page_dirty = 0;
  if(!page_remove()){
    status_message = PSTR("SD card error");
    return;
  }
  // the next page has the same index now, else the previous one
  cursor = 0;
  if(page_index < pages){
    if(page_read(page_index)){
      preview_load();
      return;
    }
  }else if(page_read(page_index - 1)){
    page_offset -= len;
    cursor = len;
    preview_load();
    return;
  }
  status_message = PSTR("SD card error");
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
  page_dirty = 1;
}

static void change_nibble(uint8_t key){
  uint8_t shift = nibble ? 0 : 4;
  uint8_t value = (data[cursor] >> shift) & 0x0F;
  value = (key == UP_KEY_PRESSED ? value + 1 : value - 1) & 0x0F;
  data[cursor] = (data[cursor] & ~(0x0F << shift)) | (value << shift);
  modified = 1;
  page_dirty = 1;
}

// the end of a page is the start of the next one: the cursor stays on the
// next page, only the last page has the end position
static void cursor_to_next_page(void){
  if(cursor == len && page_next()) cursor = 0;
}

// the last line of the page with chars: the empty line after the last '\n'
// is shown, but it is the start of the next page
static uint16_t last_text_line(void){
  uint16_t line;
  uint8_t col;
  text_locate(len, &line, &col);
  if(line && len && data[len - 1] == '\n' && page_index + 1 < pages) line--;
  return line;
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
        cursor_to_next_page();
        break;
      case C_KEY_PRESSED:
        last_char = data[cursor];
        editing = 0;
        break;
      case B_KEY_PRESSED:
        delete_byte();
        editing = 0;
        break;
      default:
        change_char(key);
    }
    return;
  }

  switch(key){
    case LEFT_KEY_PRESSED:
      if(cursor){
        cursor--;
      }else if(page_prev()){
        cursor = len ? len - 1 : 0;
      }
      break;
    case RIGHT_KEY_PRESSED:
      if(cursor < len) cursor++;
      cursor_to_next_page();
      break;
    case UP_KEY_PRESSED:
      text_locate(cursor, &line, &col);
      if(line){
        cursor = text_pos(line - 1, col);
      }else if(page_prev()){
        cursor = text_pos(last_text_line(), col);
      }
      break;
    case DOWN_KEY_PRESSED:
      text_locate(cursor, &line, &col);
      if(line < last_text_line() || page_index + 1 >= pages){
        cursor = text_pos(line + 1, col);
      }else if(page_next()){
        cursor = text_pos(0, col);
      }
      break;
    case A_KEY_PRESSED:
      // at the end: a new char
      if(cursor < len || insert_byte(last_char)) editing = 1;
      break;
    case B_KEY_PRESSED:
      if(insert_byte(last_char)) editing = 1;
      break;
  }
}

static void hex_key(uint8_t key){
  uint8_t col = cursor % HEX_ROW_BYTES;

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
        // only on the existing bytes of the page
        if(!nibble){
          nibble = 1;
        }else if(cursor + 1 < len){
          cursor++;
          nibble = 0;
        }
        break;
      case B_KEY_PRESSED:
        delete_byte();
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
      }else if(page_prev()){
        cursor = len ? len - 1 : 0;
        nibble = len ? 1 : 0;
      }
      break;
    case RIGHT_KEY_PRESSED:
      if(cursor == len) break;
      if(!nibble){
        nibble = 1;
      }else{
        cursor++;
        nibble = 0;
        cursor_to_next_page();
      }
      break;
    case UP_KEY_PRESSED:
      if(cursor >= HEX_ROW_BYTES){
        cursor -= HEX_ROW_BYTES;
      }else if(page_prev()){
        // the same column in the last row
        uint16_t last = len ? (len - 1) / HEX_ROW_BYTES * HEX_ROW_BYTES : 0;
        cursor = last + col < len ? last + col : (len ? len - 1 : 0);
      }
      break;
    case DOWN_KEY_PRESSED:
      if(cursor + HEX_ROW_BYTES < len){
        cursor += HEX_ROW_BYTES;
      }else if(page_index + 1 >= pages){
        cursor = len;
      }else if(page_next()){
        cursor = col < len ? col : (len ? len - 1 : 0);
      }
      break;
    case A_KEY_PRESSED:
      if(cursor < len || insert_byte(0)) editing = 1;
      break;
    case B_KEY_PRESSED:
      if(insert_byte(0)){
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

static uint8_t is_binary(uint8_t c){
  return c > '~' || (c < ' ' && c != '\n' && c != '\r' && c != '\t');
}

static uint8_t is_temp(const fat16_entry_t *entry){
  return entry->record_sector == temp.record_sector && entry->record_offset == temp.record_offset;
}

static void new_file(void){
  if(!can_discard()) return;
  document_clear();
  mode = MODE_TEXT;
  has_file = 0;
}

// the file by pages to the temp file, binary - there are not text bytes
static uint8_t load(const fat16_entry_t *entry, uint8_t *binary){
  fat16_file_t reader;
  uint16_t read;

  document_clear();
  pages = 0;
  *binary = 0;
  fat16_file_open(&reader, entry->cluster, entry->size);
  while(1){
    if(!fat16_file_read(&reader, sector, &read)) return 0;
    if(!read) break;
    // the page write uses the sector buffer: read the sector again after it
    uint32_t data_sector = reader.sector - 1;
    for(uint16_t i = 0; i < read; i++){
      uint8_t c = sector[i];
      if(is_binary(c)) *binary = 1;
      data[len++] = c;
      if(len == PAGE_FILL_MAX || (len >= PAGE_FILL_MIN && c == '\n')){
        page_index = pages++;
        page_dirty = 1;
        if(!page_write() || !sd_read_sector(data_sector, sector)) return 0;
        len = 0;
      }
    }
    total += read;
  }
  // the rest, an empty file has one empty page
  if(len || !pages){
    page_index = pages++;
    page_dirty = 1;
    if(!page_write()) return 0;
  }
  page_offset = 0;
  if(!page_read(0)) return 0;
  preview_load();
  return 1;
}

static void open_file(void){
  fat16_entry_t entry;
  uint8_t binary;

  if(!can_discard()) return;
  file_browser_open(sector);
  // B is not used, C in the root directory - cancel
  while(1){
    uint8_t result = file_browser_run(&entry);
    if(result == FILE_BROWSER_EXIT) return;
    if(result == FILE_BROWSER_FILE && !is_temp(&entry)) break;
  }

  draw_screen(PSTR("reading..."), NULL, PSTR(""), PSTR(""));
  display_print_line(1, entry.name);
  has_file = 0;
  if(!load(&entry, &binary)){
    message(PSTR("SD card error"), entry.name);
    document_clear();
    return;
  }
  file = entry;
  has_file = 1;
  mode = binary ? MODE_HEX : MODE_TEXT;
}

// all pages to a new chain: the page buffer collects the sectors of the file,
// the current page is read again after it
static uint8_t write_document(fat16_writer_t *writer){
  uint16_t index = 0;
  uint16_t offset = 0;
  uint16_t filled = 0;
  uint8_t error = FAT16_OK;

  if(!page_write()) return FAT16_ERROR_IO;
  fat16_writer_open(writer);
  while(index < pages && !error){
    uint32_t place;
    if(!page_sector(index, &place) || !sd_read_sector(place, sector)){
      error = FAT16_ERROR_IO;
      break;
    }
    uint16_t length = get16(sector);
    uint16_t n = length - offset;
    if(n > SD_SECTOR_SIZE - filled) n = SD_SECTOR_SIZE - filled;
    memcpy(page + filled, sector + PAGE_HEADER + offset, n);
    filled += n;
    offset += n;
    if(offset >= length){
      index++;
      offset = 0;
    }
    if(filled == SD_SECTOR_SIZE){
      error = fat16_writer_sector(writer, page, sector);
      filled = 0;
    }
  }
  if(filled && !error){
    memset(page + filled, 0, SD_SECTOR_SIZE - filled);
    error = fat16_writer_sector(writer, page, sector);
  }
  if(!page_read(page_index) && !error) error = FAT16_ERROR_IO;
  if(error) fat16_free(writer->first, sector);
  return error;
}

// the document to the file: a new chain, then the old one is freed
static uint8_t write_to(fat16_entry_t *entry){
  fat16_writer_t writer;
  uint16_t old;
  uint8_t error = write_document(&writer);
  if(error) return error;
  error = fat16_set_data(entry, writer.first, total, &old, sector);
  if(error){
    fat16_free(writer.first, sector);
    return error;
  }
  return fat16_free(old, sector);
}

// the document to a new file in the directory
static uint8_t write_new(uint16_t dir_cluster, const uint8_t *raw, fat16_entry_t *entry){
  fat16_writer_t writer;
  if(fat16_name_exists(dir_cluster, raw, sector)) return FAT16_ERROR_EXISTS;
  uint8_t error = write_document(&writer);
  if(error) return error;
  error = fat16_add_file(dir_cluster, raw, writer.first, total, entry, sector);
  if(error) fat16_free(writer.first, sector);
  return error;
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
      if(is_temp(&entry) || !sure(PSTR("replace file?"), entry.name)) continue;
      draw_screen(PSTR("saving..."), NULL, PSTR(""), PSTR(""));
      display_print_line(1, entry.name);
      uint8_t error = write_to(&entry);
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
    uint8_t error = write_new(file_browser_dir(), raw, &entry);
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
  uint8_t error = write_to(&file);
  show_error(error);
  if(!error) modified = 0;
}

// like menu_exit_app, but the temp file is deleted before
static void exit_app(void){
  if(!can_discard()) return;
  draw_screen(PSTR(""), PSTR("   EXIT APP?"), PSTR("   C - yes"), PSTR(""));
  if(wait_key() != C_KEY_PRESSED) return;
  if(!loader_is_present()){
    message(PSTR("no bootloader"), NULL);
    return;
  }
  draw_screen(PSTR(""), PSTR("   loading..."), PSTR(""), PSTR(""));
  fat16_delete(&temp, sector);
  loader_load_default_app();
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
      exit_app();
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
  while(!temp_create()){
    draw_screen(PSTR("no temp file"), PSTR("on the card"), PSTR(""), PSTR("A - retry"));
    while(wait_key() != A_KEY_PRESSED);
  }
  document_clear();
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
