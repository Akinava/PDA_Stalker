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
#include "ui.h"

// Editor of black and white Netpbm images: binary PBM ("P4") on SD card.
// The image is in the temp file /TMP/~PICED.TMP as rows of bits (1 - black,
// the high bit is the left pixel, a row takes whole bytes), only the part of
// the image on the screen is in RAM, so the image size is not limited by RAM.
//
// arrows - cursor, A - invert the pixel, B - pen: off / black / white
// (the pen paints while the cursor moves), C - menu

// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20

// screen: the image on 6 text rows, 2 status rows
#define VIEW_WIDTH 128
#define VIEW_HEIGHT 48
#define VIEW_BYTES (VIEW_WIDTH / 8)
#define INFO_ROW 6
#define STATUS_ROW 7
#define STATUS_ROWS 2
// cursor: a cross, its center shows the pixel
#define CURSOR_ARM 2

// new image
#define NEW_WIDTH 128
#define NEW_HEIGHT 64
#define IMAGE_SIZE_MIN 1
#define IMAGE_SIZE_MAX 2048
// size editor: 4 digits of width, then 4 digits of height
#define SIZE_DIGITS 4
#define SIZE_WIDTH_COL 2
#define SIZE_HEIGHT_COL 11

#define PEN_OFF 0
#define PEN_BLACK 1
#define PEN_WHITE 2
#define PENS 3

#define MENU_SAVE    0
#define MENU_SAVE_AS 1
#define MENU_NEW     2
#define MENU_OPEN    3
#define MENU_INVERT  4
#define MENU_EXIT    5
#define MENU_ITEMS   6

#define NO_SECTOR 0xFFFFFFFFUL

// temp file "/TMP/~PICED.TMP"
static const uint8_t temp_name[FAT16_RAW_NAME_SIZE] PROGMEM = "~PICED  TMP";

static uint8_t sector[SD_SECTOR_SIZE];
// pixels on the screen, also the buffer to collect sectors of files
static uint8_t view[VIEW_BYTES * VIEW_HEIGHT];

static uint16_t width;
static uint16_t height;
static uint16_t stride;           // bytes of an image row
static uint16_t view_x;           // multiple of 8
static uint16_t view_y;
static uint16_t cursor_x;
static uint16_t cursor_y;
static uint8_t pen;
static uint8_t modified;
static uint8_t view_dirty;        // the view is not written to the temp file
static fat16_entry_t file;
static uint8_t has_file;
static const char *status_message; // in flash, shown instead of the name once

static fat16_entry_t temp;
static fat16_seek_t temp_seek;
// the sector of the temp file in the sector buffer
static uint32_t cached = NO_SECTOR;
static uint32_t cached_place;
static uint8_t cached_dirty;

// lines of the screen to draw, bit per line
static uint8_t lines_dirty[VIEW_HEIGHT / 8];
static uint16_t status_hash[STATUS_ROWS];
static uint8_t status_valid;

/******************************* temp file ***********************************/

static uint8_t cache_flush(void){
  if(!cached_dirty) return 1;
  if(!sd_write_sector(cached_place, sector)) return 0;
  cached_dirty = 0;
  return 1;
}

// the sector buffer is used for other work: the cache is lost
static void cache_drop(void){
  cached = NO_SECTOR;
  cached_dirty = 0;
}

// byte of the temp file in the sector buffer, NULL on error
static uint8_t *temp_byte(uint32_t offset){
  uint32_t index = offset / SD_SECTOR_SIZE;
  if(index != cached){
    if(!cache_flush()) return NULL;
    cached = NO_SECTOR;
    if(!fat16_file_sector(temp.cluster, index, &temp_seek, &cached_place, sector)) return NULL;
    if(!sd_read_sector(cached_place, sector)) return NULL;
    cached = index;
  }
  return sector + offset % SD_SECTOR_SIZE;
}

// the temp file gets the new chain of the writer: an error only if the temp
// file does not use it (the caller frees it), the old chain is freed
static uint8_t temp_set(fat16_writer_t *writer, uint32_t size){
  uint16_t old;
  cache_drop();
  temp_seek.cluster = 0;
  uint8_t error = fat16_set_data(&temp, writer->first, size, &old, sector);
  if(!error) fat16_free(old, sector);
  return error;
}

// the old temp file is deleted, the new one is empty
static uint8_t temp_create(void){
  uint8_t raw[FAT16_RAW_NAME_SIZE];
  uint16_t dir;
  memcpy_P(raw, temp_name, sizeof(raw));
  cache_drop();
  if(fat16_tmp_dir(&dir, sector)) return 0;
  if(fat16_find(dir, raw, &temp, sector) && fat16_delete(&temp, sector)) return 0;
  if(fat16_create_file(dir, raw, NULL, 0, &temp, sector)) return 0;
  temp_seek.cluster = 0;
  return 1;
}

static uint32_t image_size(void){
  return (uint32_t)height * stride;
}

/********************************** view *************************************/

static uint8_t view_rows(void){
  return height - view_y < VIEW_HEIGHT ? height - view_y : VIEW_HEIGHT;
}

static uint8_t view_row_bytes(void){
  uint16_t left = stride - view_x / 8;
  return left < VIEW_BYTES ? left : VIEW_BYTES;
}

static void mark_all(void){
  memset(lines_dirty, 0xFF, sizeof(lines_dirty));
  status_valid = 0;
}

// copy between the view and the temp file
static uint8_t view_copy(uint8_t to_temp){
  uint8_t rows = view_rows();
  uint8_t bytes = view_row_bytes();
  for(uint8_t row = 0; row < rows; row++){
    uint32_t offset = (uint32_t)(view_y + row) * stride + view_x / 8;
    for(uint8_t i = 0; i < bytes; i++){
      uint8_t *byte = temp_byte(offset + i);
      if(!byte) return 0;
      if(to_temp){
        *byte = view[row * VIEW_BYTES + i];
        cached_dirty = 1;
      }else{
        view[row * VIEW_BYTES + i] = *byte;
      }
    }
  }
  return cache_flush();
}

static uint8_t view_load(void){
  memset(view, 0, sizeof(view));
  view_dirty = 0;
  mark_all();
  return view_copy(0);
}

static uint8_t view_store(void){
  if(!view_dirty) return 1;
  if(!view_copy(1)) return 0;
  view_dirty = 0;
  return 1;
}

// the view follows the cursor: half of the screen further
static void view_follow(void){
  uint16_t x = view_x;
  uint16_t y = view_y;
  if(cursor_x < view_x || cursor_x >= view_x + VIEW_WIDTH){
    uint16_t max = width > VIEW_WIDTH ? (width - VIEW_WIDTH + 7) & ~7 : 0;
    x = cursor_x > VIEW_WIDTH / 2 ? (cursor_x - VIEW_WIDTH / 2) & ~7 : 0;
    if(x > max) x = max;
  }
  if(cursor_y < view_y || cursor_y >= view_y + VIEW_HEIGHT){
    uint16_t max = height > VIEW_HEIGHT ? height - VIEW_HEIGHT : 0;
    y = cursor_y > VIEW_HEIGHT / 2 ? cursor_y - VIEW_HEIGHT / 2 : 0;
    if(y > max) y = max;
  }
  if(x == view_x && y == view_y) return;

  if(!view_store()) status_message = PSTR("SD card error");
  view_x = x;
  view_y = y;
  if(!view_load()) status_message = PSTR("SD card error");
}

/********************************* pixels ************************************/

static void mark_line(int16_t y){
  if(y >= 0 && y < VIEW_HEIGHT) lines_dirty[y >> 3] |= 1 << (y & 7);
}

static void mark_cursor(void){
  int16_t y = cursor_y - view_y;
  for(int8_t d = -CURSOR_ARM; d <= CURSOR_ARM; d++){
    mark_line(y + d);
  }
}

// byte of the view with the pixel at the cursor, returns the pixel mask
static uint8_t cursor_pixel(uint8_t **byte){
  uint16_t x = cursor_x - view_x;
  *byte = &view[(cursor_y - view_y) * VIEW_BYTES + x / 8];
  return 0x80 >> (x & 7);
}

static void pixel_changed(void){
  view_dirty = 1;
  modified = 1;
  mark_line(cursor_y - view_y);
}

static void set_pixel(uint8_t black){
  uint8_t *byte;
  uint8_t mask = cursor_pixel(&byte);
  if(black){
    *byte |= mask;
  }else{
    *byte &= ~mask;
  }
  pixel_changed();
}

static void invert_pixel(void){
  uint8_t *byte;
  uint8_t mask = cursor_pixel(&byte);
  *byte ^= mask;
  pixel_changed();
}

static void paint(void){
  if(pen != PEN_OFF) set_pixel(pen == PEN_BLACK);
}

/******************************** screen *************************************/

static void invert_line_pixel(uint8_t *line, int16_t x){
  if(x >= 0 && x < VIEW_WIDTH) line[x >> 3] ^= 0x80 >> (x & 7);
}

// pixels out of the image are dotted
static void draw_line(uint8_t y){
  uint8_t line[VIEW_BYTES];
  uint8_t pattern = y & 1 ? 0x00 : 0x55;
  uint16_t visible = 0;
  if(view_y + y < height){
    visible = width - view_x < VIEW_WIDTH ? width - view_x : VIEW_WIDTH;
  }

  for(uint8_t i = 0; i < VIEW_BYTES; i++){
    uint16_t x = i * 8;
    uint8_t inside = 0xFF;
    if(x >= visible){
      inside = 0;
    }else if(visible - x < 8){
      inside = 0xFF << (8 - (visible - x));
    }
    line[i] = (view[y * VIEW_BYTES + i] & inside) | (pattern & ~inside);
  }

  // cursor cross, the center is the pixel itself
  int16_t cx = cursor_x - view_x;
  int16_t dy = y - (int16_t)(cursor_y - view_y);
  if(!dy){
    for(int8_t d = 1; d <= CURSOR_ARM; d++){
      invert_line_pixel(line, cx - d);
      invert_line_pixel(line, cx + d);
    }
  }else if(dy >= -CURSOR_ARM && dy <= CURSOR_ARM){
    invert_line_pixel(line, cx);
  }
  display_gfx_line(y, line);
}

// text status row, sent only if it is changed
static void draw_status_row(uint8_t index, const char *text, uint8_t invert){
  uint16_t hash = invert;
  for(uint8_t i = 0; i < DISPLAY_GFX_COLS; i++){
    hash = hash * 31 + (uint8_t)text[i];
  }
  if((status_valid & (1 << index)) && status_hash[index] == hash) return;
  display_gfx_row(INFO_ROW + index, text, DISPLAY_GFX_COLS, DISPLAY_GFX_NO_CURSOR, invert);
  status_hash[index] = hash;
  status_valid |= 1 << index;
}

// text on the left and on the right of the row
static void status_text(char *text, const char *left, const char *right){
  memset(text, ' ', DISPLAY_GFX_COLS);
  uint8_t right_len = strlen(right);
  uint8_t left_len = strlen(left);
  uint8_t room = DISPLAY_GFX_COLS - right_len - 1;
  memcpy(text, left, left_len < room ? left_len : room);
  memcpy(text + DISPLAY_GFX_COLS - right_len, right, right_len);
}

// "X:12 Y:34   pen black" and "NAME.PBM*   128x64"
static void draw_status(void){
  static const char pens[PENS][10] PROGMEM = {"", "pen black", "pen white"};
  char text[DISPLAY_GFX_COLS];
  char left[DISPLAY_GFX_COLS + 1];
  char right[DISPLAY_GFX_COLS + 1];

  strcpy_P(left, PSTR("X:"));
  utoa(cursor_x, left + strlen(left), 10);
  strcat_P(left, PSTR(" Y:"));
  utoa(cursor_y, left + strlen(left), 10);
  strcpy_P(right, pens[pen]);
  status_text(text, left, right);
  draw_status_row(0, text, 0);

  if(status_message){
    strcpy_P(left, status_message);
    status_message = NULL;
  }else{
    if(has_file){
      strcpy(left, file.name);
    }else{
      strcpy_P(left, PSTR("new"));
    }
    if(modified) strcat_P(left, PSTR("*"));
  }
  utoa(width, right, 10);
  strcat_P(right, PSTR("x"));
  utoa(height, right + strlen(right), 10);
  status_text(text, left, right);
  draw_status_row(1, text, 1);
}

static void draw_editor(void){
  for(uint8_t y = 0; y < VIEW_HEIGHT; y++){
    if(lines_dirty[y >> 3] & (1 << (y & 7))) draw_line(y);
  }
  memset(lines_dirty, 0, sizeof(lines_dirty));
  draw_status();
}

static void editor_key(uint8_t key){
  switch(key){
    case A_KEY_PRESSED:
      invert_pixel();
      return;
    case B_KEY_PRESSED:
      pen = (pen + 1) % PENS;
      paint();
      return;
  }

  mark_cursor();
  switch(key){
    case LEFT_KEY_PRESSED:
      if(cursor_x) cursor_x--;
      break;
    case RIGHT_KEY_PRESSED:
      if(cursor_x + 1 < width) cursor_x++;
      break;
    case UP_KEY_PRESSED:
      if(cursor_y) cursor_y--;
      break;
    case DOWN_KEY_PRESSED:
      if(cursor_y + 1 < height) cursor_y++;
      break;
  }
  view_follow();
  mark_cursor();
  paint();
}

/********************************** files ************************************/

static void image_reset(uint16_t w, uint16_t h){
  width = w;
  height = h;
  stride = (w + 7) / 8;
  view_x = 0;
  view_y = 0;
  cursor_x = 0;
  cursor_y = 0;
  modified = 0;
}

// white image in the temp file, the image does not change on error
static uint8_t image_new(uint16_t w, uint16_t h){
  fat16_writer_t writer;
  uint8_t error = FAT16_OK;
  uint32_t size = (uint32_t)h * ((w + 7) / 8);

  cache_drop();
  fat16_writer_open(&writer);
  memset(view, 0, SD_SECTOR_SIZE);
  for(uint32_t done = 0; done < size && !error; done += SD_SECTOR_SIZE){
    error = fat16_writer_sector(&writer, view, sector);
  }
  if(!error) error = temp_set(&writer, size);
  if(error){
    fat16_free(writer.first, sector);
    return 0;
  }
  image_reset(w, h);
  return view_load();
}

// bytes of a file one by one, the sector is read again after other work
typedef struct {
  fat16_file_t file;
  uint32_t place;
  uint16_t pos;
  uint16_t len;
  uint8_t valid;
} reader_t;

static uint8_t reader_next(reader_t *reader, uint8_t *byte){
  if(reader->pos == reader->len){
    if(!fat16_file_read(&reader->file, sector, &reader->len) || !reader->len) return 0;
    reader->place = reader->file.sector - 1;
    reader->pos = 0;
    reader->valid = 1;
  }
  if(!reader->valid){
    if(!sd_read_sector(reader->place, sector)) return 0;
    reader->valid = 1;
  }
  *byte = sector[reader->pos++];
  return 1;
}

static uint8_t is_space(uint8_t c){
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// number of the header after spaces and comments, c - the char after it
static uint8_t read_number(reader_t *reader, uint8_t *c, uint16_t *value){
  while(is_space(*c) || *c == '#'){
    if(*c == '#'){
      while(*c != '\n'){
        if(!reader_next(reader, c)) return 0;
      }
    }
    if(!reader_next(reader, c)) return 0;
  }
  if(*c < '0' || *c > '9') return 0;
  uint32_t number = 0;
  while(*c >= '0' && *c <= '9'){
    number = number * 10 + *c - '0';
    if(number > IMAGE_SIZE_MAX) return 0;
    if(!reader_next(reader, c)) return 0;
  }
  *value = number;
  return 1;
}

// PBM header "P4 <width> <height>" and one space, then rows of bits.
// the image does not change on error
static uint8_t load(const fat16_entry_t *entry){
  fat16_writer_t writer;
  reader_t reader;
  uint8_t c;
  uint16_t w;
  uint16_t h;

  memset(&reader, 0, sizeof(reader));
  fat16_file_open(&reader.file, entry->cluster, entry->size);
  cache_drop();
  if(!reader_next(&reader, &c) || c != 'P') return 0;
  if(!reader_next(&reader, &c) || c != '4') return 0;
  if(!reader_next(&reader, &c) || !is_space(c)) return 0;
  if(!read_number(&reader, &c, &w) || !read_number(&reader, &c, &h)) return 0;
  if(!w || !h || !is_space(c)) return 0;

  uint32_t size = (uint32_t)h * ((w + 7) / 8);
  fat16_writer_open(&writer);
  uint16_t filled = 0;
  uint8_t error = FAT16_OK;
  for(uint32_t i = 0; i < size && !error; i++){
    if(!reader_next(&reader, &view[filled++])){
      error = FAT16_ERROR_IO;
      break;
    }
    if(filled == SD_SECTOR_SIZE){
      error = fat16_writer_sector(&writer, view, sector);
      reader.valid = 0;
      filled = 0;
    }
  }
  if(filled && !error){
    memset(view + filled, 0, SD_SECTOR_SIZE - filled);
    error = fat16_writer_sector(&writer, view, sector);
  }
  if(!error) error = temp_set(&writer, size);
  if(error){
    fat16_free(writer.first, sector);
    return 0;
  }
  image_reset(w, h);
  return view_load();
}

// the image as PBM to a new chain, the view is used to collect sectors
static uint8_t write_image(fat16_writer_t *writer, uint32_t *size){
  uint16_t filled;
  uint8_t error = FAT16_OK;

  if(!view_store() || !cache_flush()) return FAT16_ERROR_IO;
  fat16_writer_open(writer);

  // header "P4\n<width> <height>\n"
  strcpy_P((char *)view, PSTR("P4\n"));
  utoa(width, (char *)view + strlen((char *)view), 10);
  strcat_P((char *)view, PSTR(" "));
  utoa(height, (char *)view + strlen((char *)view), 10);
  strcat_P((char *)view, PSTR("\n"));
  filled = strlen((char *)view);
  *size = filled + image_size();

  for(uint32_t i = 0; i < image_size() && !error; i++){
    uint8_t *byte = temp_byte(i);
    if(!byte){
      error = FAT16_ERROR_IO;
      break;
    }
    view[filled++] = *byte;
    if(filled == SD_SECTOR_SIZE){
      error = fat16_writer_sector(writer, view, sector);
      cache_drop();
      filled = 0;
    }
  }
  if(filled && !error){
    memset(view + filled, 0, SD_SECTOR_SIZE - filled);
    error = fat16_writer_sector(writer, view, sector);
  }
  cache_drop();
  if(!view_load() && !error) error = FAT16_ERROR_IO;
  if(error) fat16_free(writer->first, sector);
  return error;
}

// the image to the file: a new chain, then the old one is freed
static uint8_t write_to(fat16_entry_t *entry){
  fat16_writer_t writer;
  uint32_t size;
  uint16_t old;
  uint8_t error = write_image(&writer, &size);
  if(error) return error;
  error = fat16_set_data(entry, writer.first, size, &old, sector);
  if(error){
    fat16_free(writer.first, sector);
    return error;
  }
  return fat16_free(old, sector);
}

// the image to a new file in the directory
static uint8_t write_new(uint16_t dir_cluster, const uint8_t *raw, fat16_entry_t *entry){
  fat16_writer_t writer;
  uint32_t size;
  if(fat16_name_exists(dir_cluster, raw, sector)) return FAT16_ERROR_EXISTS;
  uint8_t error = write_image(&writer, &size);
  if(error) return error;
  error = fat16_add_file(dir_cluster, raw, writer.first, size, entry, sector);
  if(error) fat16_free(writer.first, sector);
  return error;
}

static uint8_t is_temp(const fat16_entry_t *entry){
  return entry->record_sector == temp.record_sector && entry->record_offset == temp.record_offset;
}

// unsaved changes are lost: ask first
static uint8_t can_discard(void){
  return !modified || ui_sure(PSTR("discard changes?"), NULL);
}

// A on a file - write to it, B - a new file in the directory, C - cancel
static void save_as(void){
  static const uint8_t default_name[FAT16_RAW_NAME_SIZE] PROGMEM = "IMAGE   PBM";
  fat16_entry_t entry;
  uint8_t raw[FAT16_RAW_NAME_SIZE];

  display_print_screen_P(PSTR("save as:"), PSTR("A - to the file"), PSTR("B - new file"), PSTR("C - cancel"));
  if(ui_wait_key() == C_KEY_PRESSED) return;

  file_browser_open(sector);
  cache_drop();
  while(1){
    uint8_t result = file_browser_run(&entry);
    if(result == FILE_BROWSER_EXIT) return;

    if(result == FILE_BROWSER_FILE){
      if(is_temp(&entry) || !ui_sure(PSTR("replace file?"), entry.name)) continue;
      display_print_screen_P(PSTR("saving..."), NULL, PSTR(""), PSTR(""));
      display_print_line(1, entry.name);
      uint8_t error = write_to(&entry);
      if(error){
        ui_show_error(error);
        continue;
      }
      break;
    }

    // B: the name of the new file, the current name at the start
    if(has_file){
      fat16_raw_name(&file, raw);
    }else{
      memcpy_P(raw, default_name, sizeof(raw));
    }
    if(!name_edit(PSTR("file name"), raw)) continue;
    display_print_screen_P(PSTR("saving..."), PSTR(""), PSTR(""), PSTR(""));
    uint8_t error = write_new(file_browser_dir(), raw, &entry);
    if(error){
      ui_show_error(error);
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
  display_print_screen_P(PSTR("saving..."), NULL, PSTR(""), PSTR(""));
  display_print_line(1, file.name);
  cache_drop();
  // the card could be changed or the file deleted
  if(!fat16_entry_valid(&file, sector)){
    ui_message(PSTR("file not found"), file.name);
    return;
  }
  uint8_t error = write_to(&file);
  ui_show_error(error);
  if(!error) modified = 0;
}

static void open_image(void){
  fat16_entry_t entry;

  if(!can_discard()) return;
  file_browser_open(sector);
  cache_drop();
  // B is not used, C in the root directory - cancel
  while(1){
    uint8_t result = file_browser_run(&entry);
    if(result == FILE_BROWSER_EXIT) return;
    if(result == FILE_BROWSER_FILE && !is_temp(&entry)) break;
  }

  display_print_screen_P(PSTR("reading..."), NULL, PSTR(""), PSTR(""));
  display_print_line(1, entry.name);
  if(!view_store()){
    ui_message(PSTR("SD card error"), NULL);
    return;
  }
  if(!load(&entry)){
    ui_message(PSTR("not PBM (P4)"), entry.name);
    // the view was used as the buffer: the old image again
    if(!view_load()) ui_message(PSTR("SD card error"), NULL);
    return;
  }
  file = entry;
  has_file = 1;
}

// any size IMAGE_SIZE_MIN..IMAGE_SIZE_MAX by digits: "W:0128   H:0064".
// UP / DOWN - the digit, LEFT / RIGHT - the next digit, A - ok, C - cancel
static uint8_t size_edit(uint16_t *w, uint16_t *h){
  static const uint16_t powers[SIZE_DIGITS] PROGMEM = {1000, 100, 10, 1};
  // the units of the width at the start
  uint8_t digit = SIZE_DIGITS - 1;

  display_print_line_P(0, PSTR("new image size"));
  display_print_line_P(3, PSTR("A-ok C-cancel"));
  ui_wait_release();
  while(1){
    char line[DISPLAY_COLS + 1];
    uint16_t values[2] = {*w, *h};
    memset(line, ' ', DISPLAY_COLS);
    line[DISPLAY_COLS] = '\0';
    line[SIZE_WIDTH_COL - 2] = 'W';
    line[SIZE_HEIGHT_COL - 2] = 'H';
    line[SIZE_WIDTH_COL - 1] = line[SIZE_HEIGHT_COL - 1] = ':';
    for(uint8_t field = 0; field < 2; field++){
      uint8_t col = field ? SIZE_HEIGHT_COL : SIZE_WIDTH_COL;
      for(uint8_t i = 0; i < SIZE_DIGITS; i++){
        line[col + i] = '0' + values[field] / pgm_read_word(&powers[i]) % 10;
      }
    }
    display_print_line(1, line);
    // the edited digit is marked on the next line
    memset(line, ' ', DISPLAY_COLS);
    line[digit < SIZE_DIGITS ? SIZE_WIDTH_COL + digit : SIZE_HEIGHT_COL + digit - SIZE_DIGITS] = '^';
    display_print_line(2, line);

    uint8_t key;
    while((key = keys_get_repeat(POLL_INTERVAL_MS)) == NOOP){
      _delay_ms(POLL_INTERVAL_MS);
    }
    uint16_t *value = digit < SIZE_DIGITS ? w : h;
    uint16_t power = pgm_read_word(&powers[digit % SIZE_DIGITS]);
    uint8_t current = *value / power % 10;
    int32_t changed = *value;
    switch(key){
      // the digit goes round 0..9, the others do not change
      case UP_KEY_PRESSED:
        changed += current == 9 ? -9L * power : power;
        break;
      case DOWN_KEY_PRESSED:
        changed -= current == 0 ? -9L * power : power;
        break;
      case LEFT_KEY_PRESSED:
        digit = (digit + 2 * SIZE_DIGITS - 1) % (2 * SIZE_DIGITS);
        break;
      case RIGHT_KEY_PRESSED:
        digit = (digit + 1) % (2 * SIZE_DIGITS);
        break;
      case A_KEY_PRESSED:
        ui_wait_release();
        return 1;
      case C_KEY_PRESSED:
        ui_wait_release();
        return 0;
    }
    if(changed < IMAGE_SIZE_MIN) changed = IMAGE_SIZE_MIN;
    if(changed > IMAGE_SIZE_MAX) changed = IMAGE_SIZE_MAX;
    *value = changed;
  }
}

static void new_image(void){
  uint16_t w = NEW_WIDTH;
  uint16_t h = NEW_HEIGHT;
  if(!can_discard() || !size_edit(&w, &h)) return;
  display_print_screen_P(PSTR("creating..."), PSTR(""), PSTR(""), PSTR(""));
  if(!view_store() || !image_new(w, h)){
    ui_message(PSTR("SD card error"), NULL);
    view_load();
    return;
  }
  has_file = 0;
}

// black <-> white, bits after the width stay 0
static void invert_image(void){
  display_print_screen_P(PSTR("inverting..."), PSTR(""), PSTR(""), PSTR(""));
  uint8_t last_mask = 0xFF << ((8 - width % 8) % 8);
  uint8_t ok = view_store();
  for(uint16_t row = 0; row < height && ok; row++){
    for(uint16_t i = 0; i < stride; i++){
      uint8_t *byte = temp_byte((uint32_t)row * stride + i);
      if(!byte){
        ok = 0;
        break;
      }
      *byte = ~*byte & (i + 1 == stride ? last_mask : 0xFF);
      cached_dirty = 1;
    }
  }
  ok = ok && cache_flush() && view_load();
  modified = 1;
  if(!ok) ui_message(PSTR("SD card error"), NULL);
}

static void exit_app(void){
  if(!can_discard() || !ui_exit_confirm()) return;
  if(!loader_is_present()){
    ui_message(PSTR("no bootloader"), NULL);
    return;
  }
  fat16_delete(&temp, sector);
  loader_load_default_app();
}

// menu and file operations are in the text mode of the display
static void run_menu(void){
  static const char items[MENU_ITEMS][12] PROGMEM = {
    "save", "save as", "new image", "open image", "invert", "exit"
  };

  ui_wait_release();
  display_gfx_off();
  switch(ui_menu((const char *)items, sizeof(items[0]), MENU_ITEMS)){
    case MENU_SAVE:
      save();
      break;
    case MENU_SAVE_AS:
      save_as();
      break;
    case MENU_NEW:
      new_image();
      break;
    case MENU_OPEN:
      open_image();
      break;
    case MENU_INVERT:
      invert_image();
      break;
    case MENU_EXIT:
      exit_app();
      break;
  }
  ui_wait_release();
  display_gfx_on();
  mark_all();
}

/********************************** main *************************************/

int main(void){
  init_keys();
  // CSN high before any SPI traffic, otherwise radio catches display data
  init_radio();
  init_sd();
  init_display();

  ui_mount(sector);
  while(!temp_create() || !image_new(NEW_WIDTH, NEW_HEIGHT)){
    display_print_screen_P(PSTR("no temp file"), PSTR("on the card"), PSTR(""), PSTR("A - retry"));
    while(ui_wait_key() != A_KEY_PRESSED);
  }
  display_gfx_on();
  mark_all();
  draw_editor();

  while(1){
    uint8_t key = keys_get_repeat(POLL_INTERVAL_MS);
    if(key != NOOP){
      if(key == C_KEY_PRESSED){
        run_menu();
      }else{
        editor_key(key);
      }
      draw_editor();
    }
    _delay_ms(POLL_INTERVAL_MS);
  }
}
