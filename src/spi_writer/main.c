#include <string.h>
#include <util/delay.h>
#include "display.h"
#include "keys.h"
#include "radio.h"
#include "sd.h"
#include "fat16_dir.h"
#include "file_browser.h"
#include "isp.h"

// Programmer of the other board (ATmega328P) through the connector:
// flash a BIN file from SD card to the address, read / write fuses

// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20
#define AUTHOR_DELAY_MS 2000

// fuses of the SD bootloader (bootloader/Makefile): high, low, ext
#define DEFAULT_FUSES {0xDA, 0xFF, 0xFD}
// bootloader place (BOOT_ADDRESS)
#define DEFAULT_APP_ADDRESS 0x7800
// high fuse bits RSTDISBL (7) and DWEN (6) must stay unprogrammed (1),
// otherwise the chip can not be programmed by ISP any more
#define HIGH_FUSE_SAFE_MASK 0xC0
// only 3 low bits of the ext fuse exist
static const uint8_t fuse_masks[ISP_FUSES] = {0xFF, 0xFF, 0x07};

// columns of the editable hex digits on the screen
#define ADDR_DIGITS_COL 8       // " addr 0x7800"
#define ADDR_DIGITS 3           // the lowest digit is set by the page size
#define FUSE_DIGITS_COL 3       // " H:DA L:FF E:FD"
#define FUSE_STEP_COL 5

static uint8_t sector[SD_SECTOR_SIZE];
static uint8_t fuses[ISP_FUSES] = DEFAULT_FUSES;
static uint16_t app_address = DEFAULT_APP_ADDRESS;
static fat16_entry_t app_file;
static uint8_t app_chosen;

/******************************** screen *************************************/

// SD traffic corrupts display RAM, so every screen is drawn completely
// after the SD work
static void draw_screen(const char *l0, const char *l1, const char *l2, const char *l3){
  display_print_line(0, l0);
  display_print_line(1, l1);
  display_print_line(2, l2);
  display_print_line(3, l3);
}

static uint8_t wait_key(void){
  uint8_t key;
  while((key = keys_get_press()) == NOOP){
    _delay_ms(POLL_INTERVAL_MS);
  }
  return key;
}

static void message(const char *l0, const char *l1){
  draw_screen(l0, l1, "", "C - back");
  while(wait_key() != C_KEY_PRESSED);
}

static uint8_t confirm(const char *l0, const char *l1){
  draw_screen(l0, l1, "A - yes", "C - no");
  while(1){
    switch(wait_key()){
      case A_KEY_PRESSED: return 1;
      case C_KEY_PRESSED: return 0;
    }
  }
}

static char *put_hex(char *p, uint16_t value, uint8_t digits){
  static const char hex[] = "0123456789ABCDEF";
  while(digits--){
    *p++ = hex[(value >> (digits * 4)) & 0x0F];
  }
  *p = '\0';
  return p;
}

// line with '^' under the column
static void draw_marker(uint8_t row, uint8_t col){
  char line[DISPLAY_COLS + 1];
  memset(line, ' ', col);
  line[col] = '^';
  line[col + 1] = '\0';
  display_print_line(row, line);
}

// " " or ">" and the text
static void draw_item(uint8_t row, uint8_t selected, const char *text){
  char line[DISPLAY_COLS + 1];
  line[0] = selected ? '>' : ' ';
  strncpy(line + 1, text, DISPLAY_COLS - 1);
  line[DISPLAY_COLS] = '\0';
  display_print_line(row, line);
}

// UP / DOWN in the list of count items
static uint8_t move_cursor(uint8_t key, uint8_t cursor, uint8_t count){
  if(key == UP_KEY_PRESSED) return cursor ? cursor - 1 : count - 1;
  if(key == DOWN_KEY_PRESSED) return cursor < count - 1 ? cursor + 1 : 0;
  return cursor;
}

/********************************* ISP ***************************************/

// target is held in reset outside of programming, returns error or NULL
static const char *isp_connect(void){
  if(!isp_enter()) return "no chip answer";
  if(!isp_check_signature()){
    isp_pause();
    return "wrong signature";
  }
  return NULL;
}

static void draw_progress(uint32_t done, uint32_t size){
  char line[DISPLAY_COLS + 1];
  char *p = line;
  uint8_t percent = done * 100 / size;
  if(percent >= 100) *p++ = '1';
  if(percent >= 10) *p++ = '0' + percent / 10 % 10;
  *p++ = '0' + percent % 10;
  strcpy(p, " %");
  draw_screen("writing...", line, app_file.name, "");
}

// write the sector to the flash and verify it, programming mode is on
static const char *write_sector(uint16_t address, uint16_t len){
  // rest of the last page is empty flash
  memset(sector + len, 0xFF, SD_SECTOR_SIZE - len);

  for(uint16_t page = 0; page < len; page += ISP_PAGE_SIZE){
    for(uint8_t i = 0; i < ISP_PAGE_SIZE; i += 2){
      isp_load_word(i / 2, sector[page + i] | ((uint16_t)sector[page + i + 1] << 8));
    }
    if(!isp_write_page(address + page)) return "write error";
  }

  for(uint16_t i = 0; i < len; i++){
    if(isp_read_flash(address + i) != sector[i]) return "verify error";
  }
  return NULL;
}

// SD and display are used only while the target is in reset without
// programming mode, otherwise it takes their traffic as ISP instructions
static const char *write_app(void){
  fat16_file_t file;
  uint16_t address = app_address;
  uint32_t done = 0;
  uint16_t len;
  const char *error;

  draw_screen("erasing...", "", "", "");
  if((error = isp_connect())) return error;
  if(!isp_chip_erase()){
    isp_pause();
    return "erase error";
  }

  fat16_file_open(&file, app_file.cluster, app_file.size);
  while(1){
    isp_pause();
    draw_progress(done, app_file.size);
    if(!fat16_file_read(&file, sector, &len)) return "SD read error";
    if(!len) return NULL;

    if(!isp_enter()) return "no chip answer";
    error = write_sector(address, len);
    if(error){
      isp_pause();
      return error;
    }
    address += len;
    done += len;
  }
}

static const char *read_fuses(void){
  const char *error = isp_connect();
  if(error) return error;
  for(uint8_t i = 0; i < ISP_FUSES; i++){
    fuses[i] = isp_read_fuse(i);
  }
  isp_pause();
  return NULL;
}

static const char *write_fuses(void){
  const char *error = isp_connect();
  if(error) return error;
  for(uint8_t i = 0; i < ISP_FUSES && !error; i++){
    if(!isp_write_fuse(i, fuses[i])){
      error = "write error";
    }else if((isp_read_fuse(i) ^ fuses[i]) & fuse_masks[i]){
      error = "verify error";
    }
  }
  isp_pause();
  return error;
}

/******************************* load app ************************************/

static void format_address(char *line){
  strcpy(line, "addr 0x");
  put_hex(line + strlen(line), app_address, 4);
}

// UP, A - increase digit, DOWN, B - decrease, LEFT / RIGHT - digit, C - done
static void edit_address(void){
  static const uint16_t steps[ADDR_DIGITS] = {0x1000, 0x0100, ISP_PAGE_SIZE};
  char line[DISPLAY_COLS + 1];
  uint8_t digit = 0;

  display_print_line(2, "UP/DOWN - value");
  display_print_line(3, "C - done");
  while(1){
    format_address(line);
    draw_item(0, 0, line);
    draw_marker(1, ADDR_DIGITS_COL + digit);

    uint16_t step = steps[digit];
    switch(wait_key()){
      case UP_KEY_PRESSED:
      case A_KEY_PRESSED:
        if(app_address + step <= ISP_FLASH_SIZE - ISP_PAGE_SIZE) app_address += step;
        break;
      case DOWN_KEY_PRESSED:
      case B_KEY_PRESSED:
        if(app_address >= step) app_address -= step;
        break;
      case LEFT_KEY_PRESSED:
        if(digit) digit--;
        break;
      case RIGHT_KEY_PRESSED:
        if(digit < ADDR_DIGITS - 1) digit++;
        break;
      case C_KEY_PRESSED:
        return;
    }
  }
}

static uint8_t is_bin(const fat16_entry_t *entry){
  return !fat16_is_dir(entry) && !strcmp(entry->ext, "BIN");
}

static void choose_file(void){
  fat16_entry_t entry;

  draw_screen("SD init...", "", "", "");
  if(sd_init_card() == SD_TYPE_NONE){
    message("SD init error", "insert the card");
    return;
  }
  if(!fat16_mount(sector)){
    message("no FAT16", "on the card");
    return;
  }

  file_browser_open(sector);
  // only BIN files, C in the root directory - back without a file
  while(file_browser_run(&entry) == FILE_BROWSER_FILE){
    if(is_bin(&entry)){
      app_file = entry;
      app_chosen = 1;
      return;
    }
  }
}

static void start_write_app(void){
  if(!app_chosen){
    message("no file", "");
    return;
  }
  if(!app_file.size || (uint32_t)app_address + app_file.size > ISP_FLASH_SIZE){
    message(app_file.name, "file too big");
    return;
  }
  if(!confirm("erase chip and", "write app?")) return;

  const char *error = write_app();
  if(error){
    message(error, app_file.name);
  }else{
    message("write done", app_file.name);
  }
}

static void load_app_menu(void){
  char line[DISPLAY_COLS + 1];
  uint8_t cursor = 0;

  while(1){
    format_address(line);
    draw_item(0, cursor == 0, line);
    draw_item(1, cursor == 1, app_chosen ? app_file.name : "no file");
    draw_item(2, cursor == 2, "write");
    display_print_line(3, "C - back");

    uint8_t key = wait_key();
    cursor = move_cursor(key, cursor, 3);
    if(key == C_KEY_PRESSED) return;
    if(key != A_KEY_PRESSED) continue;

    switch(cursor){
      case 0: edit_address(); break;
      case 1: choose_file(); break;
      case 2: start_write_app(); break;
    }
  }
}

/********************************* fuses *************************************/

static void format_fuses(char *line){
  static const char names[ISP_FUSES] = {'H', 'L', 'E'};
  char *p = line;
  for(uint8_t i = 0; i < ISP_FUSES; i++){
    if(i) *p++ = ' ';
    *p++ = names[i];
    *p++ = ':';
    p = put_hex(p, fuses[i], 2);
  }
}

// UP, A - increase digit, DOWN, B - decrease, LEFT / RIGHT - digit, C - done
static void edit_fuses(void){
  char line[DISPLAY_COLS + 1];
  uint8_t digit = 0;

  display_print_line(2, "UP/DOWN - value");
  display_print_line(3, "C - done");
  while(1){
    format_fuses(line);
    draw_item(0, 0, line);
    draw_marker(1, FUSE_DIGITS_COL + digit / 2 * FUSE_STEP_COL + digit % 2);

    uint8_t *fuse = &fuses[digit / 2];
    // high nibble for even digit, low for odd
    uint8_t shift = digit % 2 ? 0 : 4;
    uint8_t nibble = (*fuse >> shift) & 0x0F;
    switch(wait_key()){
      case UP_KEY_PRESSED:
      case A_KEY_PRESSED:
        nibble = (nibble + 1) & 0x0F;
        break;
      case DOWN_KEY_PRESSED:
      case B_KEY_PRESSED:
        nibble = (nibble - 1) & 0x0F;
        break;
      case LEFT_KEY_PRESSED:
        if(digit) digit--;
        break;
      case RIGHT_KEY_PRESSED:
        if(digit < ISP_FUSES * 2 - 1) digit++;
        break;
      case C_KEY_PRESSED:
        return;
    }
    *fuse = (*fuse & ~(0x0F << shift)) | (nibble << shift);
  }
}

static void start_write_fuses(void){
  char line[DISPLAY_COLS + 1];

  if((fuses[ISP_FUSE_HIGH] & HIGH_FUSE_SAFE_MASK) != HIGH_FUSE_SAFE_MASK){
    message("unsafe high fuse", "ISP will be off");
    return;
  }
  format_fuses(line);
  if(!confirm("write fuses?", line)) return;

  const char *error = write_fuses();
  message(error ? error : "write done", line);
}

static void fuses_menu(void){
  static const uint8_t defaults[ISP_FUSES] = DEFAULT_FUSES;
  char line[DISPLAY_COLS + 1];
  uint8_t cursor = 0;

  while(1){
    format_fuses(line);
    draw_item(0, cursor == 0, line);
    draw_item(1, cursor == 1, "read from chip");
    draw_item(2, cursor == 2, "write to chip");
    draw_item(3, cursor == 3, "defaults");

    uint8_t key = wait_key();
    cursor = move_cursor(key, cursor, 4);
    if(key == C_KEY_PRESSED) return;
    if(key != A_KEY_PRESSED) continue;

    switch(cursor){
      case 0:
        edit_fuses();
        break;
      case 1:{
        const char *error = read_fuses();
        if(error) message(error, "");
        break;
      }
      case 2:
        start_write_fuses();
        break;
      case 3:
        memcpy(fuses, defaults, sizeof(fuses));
        break;
    }
  }
}

/********************************* main **************************************/

int main(void){
  static const char *const items[] = {"load app", "fuses"};
  uint8_t cursor = 0;

  init_keys();
  // CSN high before any SPI traffic, otherwise radio catches display data
  init_radio();
  init_sd();
  // hold the other board in reset: it does not take SPI traffic and
  // its program does not drive the SPI bus
  init_isp();
  init_display();

  while(1){
    for(uint8_t i = 0; i < DISPLAY_ROWS; i++){
      draw_item(i, i == cursor, i < 2 ? items[i] : "");
    }

    uint8_t key = wait_key();
    cursor = move_cursor(key, cursor, 2);
    if(key == C_KEY_PRESSED) menu_exit_app();
    if(key != A_KEY_PRESSED) continue;

    if(cursor == 0) load_app_menu();
    if(cursor == 1) fuses_menu();
  }
}
