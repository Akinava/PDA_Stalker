#include <stdlib.h>
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
// after the SD work
static void draw_screen(const char *l0, const char *l1, const char *l2, const char *l3){
  display_print_line(0, l0);
  display_print_line(1, l1);
  display_print_line(2, l2);
  display_print_line(3, l3);
}

// "<prefix><value><suffix>" into line of DISPLAY_COLS + 1 bytes
static char *format_value(char *line, const char *prefix, uint32_t value, const char *suffix){
  char *p = line;
  while(*prefix) *p++ = *prefix++;
  ultoa(value, p, 10);
  while(*p) p++;
  while(*suffix && p < line + DISPLAY_COLS) *p++ = *suffix++;
  *p = '\0';
  return line;
}

static uint8_t wait_key(void){
  uint8_t key;
  while((key = keys_get_press()) == NOOP){
    _delay_ms(POLL_INTERVAL_MS);
  }
  return key;
}

static void draw_progress(uint8_t percent){
  char line[DISPLAY_COLS + 1];
  draw_screen("formatting...", format_value(line, "", percent, " %"), "do not remove", "the card");
}

// returns 1 to format the card, 0 to scan it again
static uint8_t card_info(void){
  static const char *const type_names[] = {
    [SD_TYPE_NONE] = "",
    [SD_TYPE_V1]   = "type SD v1",
    [SD_TYPE_V2]   = "type SD v2",
    [SD_TYPE_SDHC] = "type SDHC",
  };
  char line[DISPLAY_COLS + 1];

  draw_screen("SD init...", "", "", "");
  uint8_t type = sd_init_card();
  uint32_t card_sectors = type != SD_TYPE_NONE ? sd_get_sectors() : 0;

  if(!card_sectors){
    draw_screen("SD init error", "insert the card", "", "A - retry");
    while(wait_key() != A_KEY_PRESSED);
    return 0;
  }

  format_value(line, "card ", card_sectors / SECTORS_PER_MB, " MB");
  if(!fat16_layout(card_sectors, &layout)){
    draw_screen(line, type_names[type], "too small", "A - retry");
    while(wait_key() != A_KEY_PRESSED);
    return 0;
  }

  draw_screen(line, type_names[type], "A - format", "B - rescan");
  while(1){
    switch(wait_key()){
      case A_KEY_PRESSED: return 1;
      case B_KEY_PRESSED: return 0;
    }
  }
}

// returns 1 if user confirmed formatting
static uint8_t confirm(void){
  char line[DISPLAY_COLS + 1];
  format_value(line, "FAT16 ", layout.sectors / SECTORS_PER_MB, " MB");
  draw_screen("erase all data?", line, "A - yes", "C - no");
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

  format_value(size_line, "FAT16 ", layout.sectors / SECTORS_PER_MB, " MB");
  // cluster size in KB: 2 sectors per KB
  format_value(cluster_line, "cluster ", layout.cluster_sectors / 2, " KB");
  if(ok){
    draw_screen("format done", size_line, cluster_line, "C - back");
  }else{
    draw_screen("format error", "card is not", "formatted", "C - back");
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
