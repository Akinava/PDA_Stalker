#include <util/delay.h>
#include "display.h"
#include "keys.h"
#include "led.h"
#include "vibro.h"

// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20
#define LED_BLINK_INTERVAL_MS 500

static void test_button(void);
static void test_led(void);
static void test_vibro(void);

typedef struct {
  const char *name;
  void (*run)(void);
} menu_item_t;

static const menu_item_t menu[] = {
  {"test button", test_button},
  {"test LED",    test_led},
  {"test vibro",  test_vibro},
};
#define MENU_SIZE (sizeof(menu) / sizeof(menu[0]))

static uint8_t selected = 0;

static void draw_menu(void){
  char line[DISPLAY_COLS + 1];
  // scroll the list if it is longer than the screen
  uint8_t first = selected < DISPLAY_ROWS ? 0 : selected - DISPLAY_ROWS + 1;

  for(uint8_t row = 0; row < DISPLAY_ROWS; row++){
    uint8_t item = first + row;
    if(item >= MENU_SIZE){
      display_print_line(row, "");
      continue;
    }
    line[0] = item == selected ? '>' : ' ';
    uint8_t i = 1;
    for(const char *name = menu[item].name; *name && i < DISPLAY_COLS; i++){
      line[i] = *name++;
    }
    line[i] = '\0';
    display_print_line(row, line);
  }
}

// wait for button C to return to the main menu
static void wait_back(void){
  while(keys_get_press() != C_KEY_PRESSED){
    _delay_ms(POLL_INTERVAL_MS);
  }
}

static void test_button(void){
  static const char *const key_names[] = {
    [A_KEY_PRESSED]     = "A",
    [B_KEY_PRESSED]     = "B",
    [C_KEY_PRESSED]     = "C",
    [UP_KEY_PRESSED]    = "UP",
    [DOWN_KEY_PRESSED]  = "DOWN",
    [LEFT_KEY_PRESSED]  = "LEFT",
    [RIGHT_KEY_PRESSED] = "RIGHT",
  };

  display_clear();
  display_print_line(0, "press any button");

  // name is shown while the key is held, redraw only on change
  uint8_t shown_key = NOOP;
  while(1){
    uint8_t key = keys_read();
    if(key == C_KEY_PRESSED) return;
    if(key != shown_key){
      display_print_line(1, key == NOOP ? "" : key_names[key]);
      shown_key = key;
    }
    _delay_ms(POLL_INTERVAL_MS);
  }
}

static void test_led(void){
  uint16_t elapsed_ms = 0;

  display_clear();
  display_print_line(0, "LED blinking");
  display_print_line(1, "C - back");

  while(keys_get_press() != C_KEY_PRESSED){
    if(elapsed_ms >= LED_BLINK_INTERVAL_MS){
      TOGGLE(LED_PORT, LED_PIN);
      elapsed_ms = 0;
    }
    _delay_ms(POLL_INTERVAL_MS);
    elapsed_ms += POLL_INTERVAL_MS;
  }
  SET_LOW(LED_PORT, LED_PIN);
}

static void test_vibro(void){
  display_clear();
  display_print_line(0, "vibro on");
  display_print_line(1, "C - back");

  SET_HIGH(VIBRO_PORT, VIBRO_PIN);
  wait_back();
  SET_LOW(VIBRO_PORT, VIBRO_PIN);
}

int main(void){
  init_keys();
  init_led();
  init_vibro();
  init_display();
  draw_menu();

  while(1){
    switch(keys_get_press()){
      case UP_KEY_PRESSED:
        selected = selected ? selected - 1 : MENU_SIZE - 1;
        draw_menu();
        break;
      case DOWN_KEY_PRESSED:
        selected = selected < MENU_SIZE - 1 ? selected + 1 : 0;
        draw_menu();
        break;
      case A_KEY_PRESSED:
        menu[selected].run();
        display_clear();
        draw_menu();
        break;
    }
    _delay_ms(POLL_INTERVAL_MS);
  }
}
