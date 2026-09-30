#include <string.h>
#include <util/delay.h>
#include "ui.h"
#include "display.h"
#include "keys.h"
#include "sd.h"
#include "fat16_dir.h"
#include "fat16_edit.h"

uint8_t ui_wait_key(void){
  uint8_t key;
  while((key = keys_get_press()) == NOOP){
    _delay_ms(UI_POLL_INTERVAL_MS);
  }
  return key;
}

// wait until all keys are released: keys_get_press and keys_get_repeat
// must not see the same press
void ui_wait_release(void){
  while(keys_read() != NOOP){
    _delay_ms(UI_POLL_INTERVAL_MS);
  }
  keys_get_press();
  keys_get_repeat(UI_POLL_INTERVAL_MS);
}

// l0 (flash) and the name (RAM, may be NULL) on line 1, C - back
void ui_message(const char *l0, const char *ram_l1){
  display_print_screen_P(l0, ram_l1 ? NULL : PSTR(""), PSTR(""), PSTR("C - back"));
  if(ram_l1) display_print_line(1, ram_l1);
  while(ui_wait_key() != C_KEY_PRESSED);
}

// modal question: returns 1 on A, 0 on C
uint8_t ui_sure(const char *l0, const char *ram_l1){
  display_print_screen_P(l0, ram_l1 ? NULL : PSTR(""), PSTR("A - yes"), PSTR("C - no"));
  if(ram_l1) display_print_line(1, ram_l1);
  while(1){
    switch(ui_wait_key()){
      case A_KEY_PRESSED: return 1;
      case C_KEY_PRESSED: return 0;
    }
  }
}

// text of FAT16_ERROR_*, nothing for FAT16_OK
void ui_show_error(uint8_t error){
  static const char texts[][17] PROGMEM = {
    [FAT16_OK]             = "",
    [FAT16_ERROR_IO]       = "SD card error",
    [FAT16_ERROR_FULL]     = "card is full",
    [FAT16_ERROR_DIR_FULL] = "root dir is full",
    [FAT16_ERROR_EXISTS]   = "name exists",
    [FAT16_ERROR_SAME_DIR] = "same directory",
    [FAT16_ERROR_INSIDE]   = "into itself",
    [FAT16_ERROR_DEPTH]    = "too deep dirs",
    [FAT16_ERROR_NOT_DIR]  = "not a directory",
  };
  if(error < sizeof(texts) / sizeof(texts[0]) && error) ui_message(texts[error], NULL);
}

// list of count items in flash (char items[count][item_size]):
// UP / DOWN, A - returns the item, C - UI_NONE
uint8_t ui_menu(const char *items, uint8_t item_size, uint8_t count){
  uint8_t selected = 0;
  uint8_t first = 0;

  while(1){
    // keep selected item on the screen
    if(selected < first) first = selected;
    if(selected >= first + DISPLAY_ROWS) first = selected - DISPLAY_ROWS + 1;

    for(uint8_t row = 0; row < DISPLAY_ROWS; row++){
      char line[DISPLAY_COLS + 1];
      uint8_t item = first + row;
      line[0] = '\0';
      if(item < count){
        line[0] = item == selected ? '>' : ' ';
        strncpy_P(line + 1, items + item * item_size, DISPLAY_COLS - 1);
        line[DISPLAY_COLS] = '\0';
      }
      display_print_line(row, line);
    }

    switch(ui_wait_key()){
      case UP_KEY_PRESSED:
        selected = selected ? selected - 1 : count - 1;
        break;
      case DOWN_KEY_PRESSED:
        selected = selected < count - 1 ? selected + 1 : 0;
        break;
      case A_KEY_PRESSED:
        return selected;
      case C_KEY_PRESSED:
        return UI_NONE;
    }
  }
}

// like menu_exit_app, but the application exits itself: returns 1 on C
uint8_t ui_exit_confirm(void){
  display_print_screen_P(PSTR(""), PSTR("   EXIT APP?"), PSTR("   C - yes"), PSTR(""));
  if(ui_wait_key() != C_KEY_PRESSED) return 0;
  display_print_screen_P(PSTR(""), PSTR("   loading..."), PSTR(""), PSTR(""));
  return 1;
}

// init the card and FAT16, retry until success
void ui_mount(uint8_t *buf){
  while(1){
    display_print_screen_P(PSTR("SD init..."), PSTR(""), PSTR(""), PSTR(""));
    if(sd_init_card() == SD_TYPE_NONE){
      display_print_screen_P(PSTR("SD init error"), PSTR("insert the card"), PSTR(""), PSTR("A - retry"));
    }else if(!fat16_mount(buf)){
      display_print_screen_P(PSTR("no FAT16"), PSTR("on the card"), PSTR(""), PSTR("A - retry"));
    }else{
      return;
    }
    while(ui_wait_key() != A_KEY_PRESSED);
  }
}
