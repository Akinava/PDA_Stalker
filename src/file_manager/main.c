#include <string.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "display.h"
#include "keys.h"
#include "radio.h"
#include "sd.h"
#include "fat16_dir.h"
#include "fat16_edit.h"
#include "file_browser.h"
#include "loader.h"

// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20

// clipboard of move / copy
#define CLIP_NONE 0
#define CLIP_MOVE 1
#define CLIP_COPY 2

// context menu actions
#define ACTION_PASTE  0
#define ACTION_MOVE   1
#define ACTION_COPY   2
#define ACTION_RENAME 3
#define ACTION_DELETE 4
#define ACTIONS       5
#define ACTION_NONE   0xFF

// chars of short (8.3) names for rename, space is the end of name
#define NAME_CHARS " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-~!#$%&'()@^{}"
// "[NAME    .EXT]": name from column 1, ext from column 10
#define NAME_FIELD_COL 1

static uint8_t sector[SD_SECTOR_SIZE];
static uint8_t card_type;
static fat16_entry_t clip;
static uint8_t clip_mode = CLIP_NONE;

// SD traffic corrupts display RAM, so every screen is drawn completely
// after the SD work: lines are in flash, NULL line is printed from RAM
#define draw_screen display_print_screen_P

// l0, l2, l3 are in flash, name is in RAM (may be NULL) on line 1
static void draw_name_screen(const char *l0, const char *name, const char *l2, const char *l3){
  draw_screen(l0, NULL, l2, l3);
  if(name){
    display_print_line(1, name);
  }else{
    display_print_line_P(1, PSTR(""));
  }
}

static uint8_t wait_key(void){
  uint8_t key;
  while((key = keys_get_press()) == NOOP){
    _delay_ms(POLL_INTERVAL_MS);
  }
  return key;
}

// l0, l1 are in flash
static void error(const char *l0, const char *l1){
  draw_screen(l0, l1, PSTR(""), PSTR("A - retry"));
  while(wait_key() != A_KEY_PRESSED);
}

// init card and FAT16, retry until success
static void mount(void){
  while(1){
    draw_screen(PSTR("SD init..."), PSTR(""), PSTR(""), PSTR(""));
    card_type = sd_init_card();
    if(card_type == SD_TYPE_NONE){
      error(PSTR("SD init error"), PSTR("insert the card"));
    }else if(!fat16_mount(sector)){
      error(PSTR("no FAT16"), PSTR("on the card"));
    }else{
      return;
    }
  }
}

static uint8_t is_bin(const fat16_entry_t *entry){
  return !fat16_is_dir(entry) && !strcmp_P(entry->ext, PSTR("BIN"));
}

// ask and flash the application, returns only if cancelled or impossible
static void flash_app(const fat16_entry_t *entry){
  if(!loader_is_present()){
    error(PSTR("no bootloader"), PSTR(""));
    return;
  }
  if(card_type != SD_TYPE_SDHC){
    error(PSTR("bootloader needs"), PSTR("SDHC card"));
    return;
  }
  if(!entry->size || entry->size > LOADER_APP_MAX_SIZE){
    draw_name_screen(PSTR("bad size"), entry->name, PSTR(""), PSTR("A - retry"));
    while(wait_key() != A_KEY_PRESSED);
    return;
  }

  draw_name_screen(PSTR("flash app?"), entry->name, PSTR("A - yes"), PSTR("C - no"));
  while(1){
    switch(wait_key()){
      case A_KEY_PRESSED:
        draw_name_screen(PSTR("flashing..."), entry->name, PSTR(""), PSTR(""));
        // bootloader writes flash and resets MCU
        loader_load_app_by_cluster(entry->cluster, entry->size);
      case C_KEY_PRESSED:
        return;
    }
  }
}

/***************************** context menu **********************************/

// l0 is in flash, name is in RAM (may be NULL)
static void message(const char *l0, const char *name){
  draw_name_screen(l0, name, PSTR(""), PSTR("C - back"));
  while(wait_key() != C_KEY_PRESSED);
}

// modal window, l0 is in RAM, returns 1 on A
static uint8_t sure(const char *l0){
  draw_screen(NULL, PSTR("sure?"), PSTR("A - yes"), PSTR("C - cancel"));
  display_print_line(0, l0);
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

// "<prefix><name>" cut to the screen width, prefix is in flash
static void action_line(char *line, const char *prefix, const char *name){
  strcpy_P(line, prefix);
  strncat(line, name, DISPLAY_COLS - strlen(line));
}

static uint8_t same_record(const fat16_entry_t *a, const fat16_entry_t *b){
  return a->record_sector == b->record_sector && a->record_offset == b->record_offset;
}

// list of actions, returns the chosen one or ACTION_NONE on C
static uint8_t context_menu(const fat16_entry_t *entry){
  static const char names[ACTIONS][7] PROGMEM = {"", "move", "copy", "rename", "delete"};
  char paste[DISPLAY_COLS + 1];
  uint8_t actions[ACTIONS];
  uint8_t count = 0;
  uint8_t selected = 0;
  uint8_t first = 0;

  // paste is the first after move / copy
  if(clip_mode != CLIP_NONE){
    action_line(paste, PSTR("paste "), clip.name);
    actions[count++] = ACTION_PASTE;
  }
  if(entry->name[0]){
    for(uint8_t action = ACTION_MOVE; action < ACTIONS; action++){
      actions[count++] = action;
    }
  }
  if(!count) return ACTION_NONE;

  while(1){
    // keep selected item on the screen
    if(selected < first) first = selected;
    if(selected >= first + DISPLAY_ROWS) first = selected - DISPLAY_ROWS + 1;

    for(uint8_t row = 0; row < DISPLAY_ROWS; row++){
      char line[DISPLAY_COLS + 1];
      uint8_t item = first + row;
      line[0] = '\0';
      if(item < count){
        uint8_t action = actions[item];
        line[0] = item == selected ? '>' : ' ';
        line[1] = '\0';
        if(action == ACTION_PASTE){
          strncat(line, paste, DISPLAY_COLS - 1);
        }else{
          strncat_P(line, names[action], DISPLAY_COLS - 1);
        }
      }
      display_print_line(row, line);
    }

    switch(wait_key()){
      case UP_KEY_PRESSED:
        selected = selected ? selected - 1 : count - 1;
        break;
      case DOWN_KEY_PRESSED:
        selected = selected < count - 1 ? selected + 1 : 0;
        break;
      case A_KEY_PRESSED:
        return actions[selected];
      case C_KEY_PRESSED:
        return ACTION_NONE;
    }
  }
}

static void paste(void){
  char line[DISPLAY_COLS + 1];
  action_line(line, PSTR("paste "), clip.name);
  if(!sure(line)) return;

  draw_name_screen(clip_mode == CLIP_MOVE ? PSTR("moving...") : PSTR("copying..."),
                   clip.name, PSTR(""), PSTR(""));
  // the source could be deleted or renamed after move / copy
  if(!fat16_entry_valid(&clip, sector)){
    clip_mode = CLIP_NONE;
    message(PSTR("source changed"), clip.name);
    return;
  }

  uint8_t error;
  if(clip_mode == CLIP_MOVE){
    error = fat16_move(&clip, file_browser_dir(), sector);
    // the source is not on its place any more
    if(!error) clip_mode = CLIP_NONE;
  }else{
    // copy can be pasted again
    error = fat16_copy(&clip, file_browser_dir(), sector);
  }
  show_error(error);
}

static void delete(const fat16_entry_t *entry){
  char line[DISPLAY_COLS + 1];
  action_line(line, PSTR("delete "), entry->name);
  if(!sure(line)) return;

  draw_name_screen(PSTR("deleting..."), entry->name, PSTR(""), PSTR(""));
  if(clip_mode != CLIP_NONE && same_record(&clip, entry)) clip_mode = CLIP_NONE;
  show_error(fat16_delete(entry, sector));
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

// UP / DOWN - char, LEFT / RIGHT - position, A - ok, C - cancel
static void rename(const fat16_entry_t *entry){
  static const char chars[] PROGMEM = NAME_CHARS;
  uint8_t raw[FAT16_RAW_NAME_SIZE];
  uint8_t old[FAT16_RAW_NAME_SIZE];
  uint8_t pos = 0;

  fat16_raw_name(entry, raw);
  memcpy(old, raw, sizeof(raw));
  display_print_line_P(0, PSTR("rename"));
  display_print_line_P(3, PSTR("A-ok C-cancel"));

  while(1){
    draw_name_field(raw, pos);

    const char *c = strchr_P(chars, raw[pos]);
    uint8_t index = c ? c - chars : 0;
    uint8_t key = wait_key();
    if(key == C_KEY_PRESSED) return;
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
    message(PSTR("empty name"), NULL);
    return;
  }
  if(!memcmp(raw, old, sizeof(raw))) return;

  draw_name_screen(PSTR("renaming..."), entry->name, PSTR(""), PSTR(""));
  uint8_t error = fat16_rename(entry, raw, sector);
  // keep the clipboard valid: new name of the same record
  if(!error && clip_mode != CLIP_NONE && same_record(&clip, entry)){
    if(sd_read_sector(clip.record_sector, sector)){
      fat16_entry_from_record(&clip, sector + clip.record_offset);
    }
  }
  show_error(error);
}

static void run_action(uint8_t action, const fat16_entry_t *entry){
  switch(action){
    case ACTION_PASTE:
      paste();
      break;
    case ACTION_MOVE:
      clip = *entry;
      clip_mode = CLIP_MOVE;
      break;
    case ACTION_COPY:
      clip = *entry;
      clip_mode = CLIP_COPY;
      break;
    case ACTION_RENAME:
      rename(entry);
      break;
    case ACTION_DELETE:
      delete(entry);
      break;
  }
}

/********************************* main **************************************/

int main(void){
  fat16_entry_t entry;

  init_keys();
  // CSN high before any SPI traffic, otherwise radio catches display data
  init_radio();
  init_sd();
  init_display();

  mount();
  file_browser_open(sector);

  while(1){
    // C in the root directory does nothing
    switch(file_browser_run(&entry)){
      case FILE_BROWSER_FILE:
        if(is_bin(&entry)) flash_app(&entry);
        break;
      case FILE_BROWSER_MENU:
        run_action(context_menu(&entry), &entry);
        file_browser_reload();
        break;
    }
  }
}
