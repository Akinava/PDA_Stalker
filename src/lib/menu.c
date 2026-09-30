#include <util/delay.h>
#include "menu.h"
#include "display.h"
#include "keys.h"
#include "loader.h"

#define MENU_POLL_INTERVAL_MS 20

void menu_exit_app(void){
  uint8_t key;

  display_print_line_P(1, PSTR("   EXIT APP?"));
  display_print_line_P(2, PSTR("   C - yes"));
  while((key = keys_get_press()) == NOOP){
    _delay_ms(MENU_POLL_INTERVAL_MS);
  }

  if(key == C_KEY_PRESSED && loader_is_present()){
    display_print_line_P(1, PSTR("   loading..."));
    display_print_line_P(2, PSTR(""));
    loader_load_default_app();
  }
}
