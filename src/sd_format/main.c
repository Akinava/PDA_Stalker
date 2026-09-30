#include <stdlib.h>
#include <string.h>
#include <util/delay.h>
#include "display.h"
#include "keys.h"
#include "radio.h"
#include "sd.h"
#include "fat16.h"

// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20
// 2048 sectors of 512 bytes = 1 MB
#define SECTORS_PER_MB 2048

static uint8_t sector[SD_SECTOR_SIZE];
static fat16_layout_t layout;

// SD traffic corrupts display RAM, so every screen is drawn completely
// after the SD work: lines are in flash, NULL line is printed from RAM
#define draw_screen display_print_screen_P

// "<prefix><value><suffix>" into line of DISPLAY_COLS + 1 bytes,
// prefix and suffix are in flash
static char *format_value(char *line, const char *prefix, uint32_t value, const char *suffix){
  strcpy_P(line, prefix);
  char *p = line + strlen(line);
  ultoa(value, p, 10);
  p += strlen(p);
  strncpy_P(p, suffix, line + DISPLAY_COLS - p);
  line[DISPLAY_COLS] = '\0';
  return line;
}

static uint8_t wait_key(void){
  uint8_t key;
  while((key = keys_get_press()) == NOOP){
    _delay_ms(POLL_INTERVAL_MS);
  }
  return key;
}

// A - retry, C - exit app window; after the window the card is scanned again
static void wait_retry(void){
  while(1){
    switch(wait_key()){
      case A_KEY_PRESSED: return;
      case C_KEY_PRESSED: menu_exit_app(); return;
    }
  }
}

static void draw_progress(uint8_t percent){
  char line[DISPLAY_COLS + 1];
  draw_screen(PSTR("formatting..."), NULL, PSTR("do not remove"), PSTR("the card"));
  display_print_line(1, format_value(line, PSTR(""), percent, PSTR(" %")));
}

// returns 1 to format the card, 0 to scan it again
static uint8_t card_info(void){
  static const char type_names[][11] PROGMEM = {
    [SD_TYPE_NONE] = "",
    [SD_TYPE_V1]   = "type SD v1",
    [SD_TYPE_V2]   = "type SD v2",
    [SD_TYPE_SDHC] = "type SDHC",
  };
  char line[DISPLAY_COLS + 1];

  draw_screen(PSTR("SD init..."), PSTR(""), PSTR(""), PSTR(""));
  uint8_t type = sd_init_card();
  uint32_t card_sectors = type != SD_TYPE_NONE ? sd_get_sectors() : 0;

  if(!card_sectors){
    draw_screen(PSTR("SD init error"), PSTR("insert the card"), PSTR(""), PSTR("A - retry"));
    wait_retry();
    return 0;
  }

  format_value(line, PSTR("card "), card_sectors / SECTORS_PER_MB, PSTR(" MB"));
  if(!fat16_layout(card_sectors, &layout)){
    draw_screen(NULL, type_names[type], PSTR("too small"), PSTR("A - retry"));
    display_print_line(0, line);
    wait_retry();
    return 0;
  }

  draw_screen(NULL, type_names[type], PSTR("A - format"), PSTR("B - rescan"));
  display_print_line(0, line);
  while(1){
    switch(wait_key()){
      case A_KEY_PRESSED: return 1;
      case B_KEY_PRESSED: return 0;
      // window closed: card is scanned and the screen is drawn again
      case C_KEY_PRESSED: menu_exit_app(); return 0;
    }
  }
}

// returns 1 if user confirmed formatting
static uint8_t confirm(void){
  char line[DISPLAY_COLS + 1];
  format_value(line, PSTR("FAT16 "), layout.sectors / SECTORS_PER_MB, PSTR(" MB"));
  draw_screen(PSTR("erase all data?"), NULL, PSTR("A - yes"), PSTR("C - no"));
  display_print_line(1, line);
  while(1){
    switch(wait_key()){
      case A_KEY_PRESSED: return 1;
      case C_KEY_PRESSED: return 0;
    }
  }
}

static void format(void){
  char size_line[DISPLAY_COLS + 1];
  char cluster_line[DISPLAY_COLS + 1];

  draw_progress(0);
  uint8_t ok = fat16_format(&layout, sector, draw_progress);

  format_value(size_line, PSTR("FAT16 "), layout.sectors / SECTORS_PER_MB, PSTR(" MB"));
  // cluster size in KB: 2 sectors per KB
  format_value(cluster_line, PSTR("cluster "), layout.cluster_sectors / 2, PSTR(" KB"));
  if(ok){
    draw_screen(PSTR("format done"), NULL, NULL, PSTR("C - back"));
    display_print_line(1, size_line);
    display_print_line(2, cluster_line);
  }else{
    draw_screen(PSTR("format error"), PSTR("card is not"), PSTR("formatted"), PSTR("C - back"));
  }
  while(wait_key() != C_KEY_PRESSED);
}

int main(void){
  init_keys();
  // CSN high before any SPI traffic, otherwise radio catches display data
  init_radio();
  init_sd();
  init_display();

  while(1){
    if(card_info() && confirm()){
      format();
    }
  }
}
