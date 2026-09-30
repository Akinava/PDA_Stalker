#include <string.h>
#include <util/delay.h>
#include "file_browser.h"
#include "display.h"
#include "keys.h"

// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20

typedef struct {
  uint16_t cluster;
  uint16_t selected;
  uint16_t first;
} dir_state_t;

static uint8_t *sector;

// current directory and the path to it
static dir_state_t dir;
static dir_state_t dir_stack[FILE_BROWSER_DEPTH];
static uint8_t dir_depth;
static uint16_t dir_count;

// entries on the screen
static fat16_entry_t entries[DISPLAY_ROWS];
static uint8_t entries_count;

static void open_dir(uint16_t cluster){
  dir.cluster = cluster;
  dir.selected = 0;
  dir.first = 0;
  dir_count = fat16_dir_count(cluster, sector);
}

// SD traffic corrupts display RAM, so the whole screen is drawn after it
static void draw_list(void){
  char line[DISPLAY_COLS + 1];

  // keep selected entry on the screen
  if(dir.selected < dir.first) dir.first = dir.selected;
  if(dir.selected >= dir.first + DISPLAY_ROWS) dir.first = dir.selected - DISPLAY_ROWS + 1;

  entries_count = fat16_dir_read(dir.cluster, dir.first, entries, DISPLAY_ROWS, sector);

  for(uint8_t row = 0; row < DISPLAY_ROWS; row++){
    if(row >= entries_count){
      display_print_line(row, !entries_count && !row ? "(empty)" : "");
      continue;
    }
    fat16_entry_t *entry = &entries[row];
    line[0] = dir.first + row == dir.selected ? '>' : ' ';
    strcpy(line + 1, entry->name);
    if(fat16_is_dir(entry)){
      uint8_t len = strlen(line);
      line[len++] = FILE_BROWSER_DIR_MARK;
      line[len] = '\0';
    }
    display_print_line(row, line);
  }
}

void file_browser_open(uint8_t *buf){
  sector = buf;
  dir_depth = 0;
  open_dir(FAT16_ROOT_CLUSTER);
}

// browse until a file is chosen or C is pressed in the root directory;
// the next call continues from the same place
uint8_t file_browser_run(fat16_entry_t *entry){
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
      case A_KEY_PRESSED:{
        if(!entries_count) break;
        fat16_entry_t *selected = &entries[dir.selected - dir.first];
        if(!fat16_is_dir(selected)){
          *entry = *selected;
          return FILE_BROWSER_FILE;
        }
        if(dir_depth == FILE_BROWSER_DEPTH) break;
        dir_stack[dir_depth++] = dir;
        open_dir(selected->cluster);
        draw_list();
        break;
      }
      case C_KEY_PRESSED:
        if(!dir_depth) return FILE_BROWSER_EXIT;
        dir = dir_stack[--dir_depth];
        dir_count = fat16_dir_count(dir.cluster, sector);
        draw_list();
        break;
    }
    _delay_ms(POLL_INTERVAL_MS);
  }
}
