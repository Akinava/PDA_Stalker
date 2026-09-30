#include <util/delay.h>
#include "display.h"
#include "led.h"
#include "keys.h"

#define BLINK_INTERVAL_MS 500
// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20

static void draw(void){
  display_clear();
  display_print_at(0, 0, "ABCabc123");
}

int main(void){
  uint16_t elapsed_ms = 0;

  init_led();
  init_keys();
  init_display();
  draw();

  // LED blinks: the program is running
  while(1){
    if(keys_get_press() == C_KEY_PRESSED){
      menu_exit_app();
      draw();
    }
    if(elapsed_ms >= BLINK_INTERVAL_MS){
      TOGGLE(LED_PORT, LED_PIN);
      elapsed_ms = 0;
    }
    _delay_ms(POLL_INTERVAL_MS);
    elapsed_ms += POLL_INTERVAL_MS;
  }
}
