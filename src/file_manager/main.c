#include <string.h>
#include <util/delay.h>
#include "display.h"
#include "keys.h"
#include "radio.h"
#include "sd.h"
#include "fat16_dir.h"
#include "loader.h"

// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20
// max depth of nested directories
#define DIR_STACK_SIZE 8
#define DIR_MARK '/'

typedef struct {
  uint16_t cluster;
  uint16_t selected;
  uint16_t first;
} dir_state_t;

static uint8_t sector[SD_SECTOR_SIZE];
static uint8_t card_type;

// current directory and the path to it
static dir_state_t dir;
static dir_state_t dir_stack[DIR_STACK_SIZE];
static uint8_t dir_depth;
static uint16_t dir_count;

// entries on the screen
static fat16_entry_t entries[DISPLAY_ROWS];
static uint8_t entries_count;

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

static void error(const char *l0, const char *l1){
  draw_screen(l0, l1, "", "A - retry");
  while(wait_key() != A_KEY_PRESSED);
}

// init card and FAT16, retry until success
static void mount(void){
  while(1){
    draw_screen("SD init...", "", "", "");
    card_type = sd_init_card();
    if(card_type == SD_TYPE_NONE){
      error("SD init error", "insert the card");
    }else if(!fat16_mount(sector)){
      error("no FAT16", "on the card");
    }else{
      return;
    }
  }
}

static void open_dir(uint16_t cluster){
  dir.cluster = cluster;
  dir.selected = 0;
  dir.first = 0;
  dir_count = fat16_dir_count(cluster, sector);
}

static void draw_list(void){
  char line[DISPLAY_COLS + 1];

  // keep selected entry on the screen
  if(dir.selected < dir.first) dir.first = dir.selected;
  if(dir.selected >= dir.first + DISPLAY_ROWS) dir.first = dir.selected - DISPLAY_ROWS + 1;

  entries_count = fat16_dir_read(dir.cluster, dir.first, entries, DISPLAY_ROWS, sector);

  if(!entries_count){
    draw_screen("(empty)", "", "", "");
    return;
  }
  for(uint8_t row = 0; row < DISPLAY_ROWS; row++){
    if(row >= entries_count){
      display_print_line(row, "");
      continue;
    }
    fat16_entry_t *entry = &entries[row];
    line[0] = dir.first + row == dir.selected ? '>' : ' ';
    strcpy(line + 1, entry->name);
    if(fat16_is_dir(entry)){
      uint8_t len = strlen(line);
      line[len++] = DIR_MARK;
      line[len] = '\0';
    }
    display_print_line(row, line);
  }
}

static uint8_t is_bin(const fat16_entry_t *entry){
  return !fat16_is_dir(entry) && !strcmp(entry->ext, "BIN");
}

// ask and flash the application, returns only if cancelled or impossible
static void flash_app(const fat16_entry_t *entry){
  if(!loader_is_present()){
    error("no bootloader", "");
    return;
  }
  if(card_type != SD_TYPE_SDHC){
    error("bootloader needs", "SDHC card");
    return;
  }
  if(!entry->size || entry->size > LOADER_APP_MAX_SIZE){
    error(entry->name, "bad size");
    return;
  }

  draw_screen("flash app?", entry->name, "A - yes", "C - no");
  while(1){
    switch(wait_key()){
      case A_KEY_PRESSED:
        draw_screen("flashing...", entry->name, "", "");
        // bootloader writes flash and resets MCU
        loader_load_app_by_cluster(entry->cluster, entry->size);
      case C_KEY_PRESSED:
        return;
    }
  }
}

static void select_entry(void){
  if(!entries_count) return;
  fat16_entry_t *entry = &entries[dir.selected - dir.first];

  if(fat16_is_dir(entry)){
    if(dir_depth == DIR_STACK_SIZE) return;
    dir_stack[dir_depth++] = dir;
    open_dir(entry->cluster);
  }else if(is_bin(entry)){
    flash_app(entry);
  }
}

static void parent_dir(void){
  if(!dir_depth) return;
  dir = dir_stack[--dir_depth];
  dir_count = fat16_dir_count(dir.cluster, sector);
}

int main(void){
  init_keys();
  // CSN high before any SPI traffic, otherwise radio catches display data
  init_radio();
  init_sd();
  init_display();

  mount();
  open_dir(FAT16_ROOT_CLUSTER);
  draw_list();

  while(1){
    switch(keys_get_press()){
      case UP_KEY_PRESSED:
        if(!dir_count) break;
        dir.selected = dir.selected ? dir.selected - 1 : dir_count - 1;
        draw_list();
        break;
      case DOWN_KEY_PRESSED:
        if(!dir_count) break;
        dir.selected = dir.selected < dir_count - 1 ? dir.selected + 1 : 0;
        draw_list();
        break;
      case A_KEY_PRESSED:
        select_entry();
        draw_list();
        break;
      case C_KEY_PRESSED:
        parent_dir();
        draw_list();
        break;
    }
    _delay_ms(POLL_INTERVAL_MS);
  }
}
