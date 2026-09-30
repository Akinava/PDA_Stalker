#include <util/delay.h>
#include "display.h"
#include "led.h"

#define BLINK_INTERVAL_MS 500

int main(void){
  init_led();
  init_display();

  display_print_at(0, 0, "ABCabc123");

  // LED blinks: the program is running
  while(1){
    TOGGLE(LED_PORT, LED_PIN);
    _delay_ms(BLINK_INTERVAL_MS);
  }
}
