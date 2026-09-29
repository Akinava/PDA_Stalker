#include <util/delay.h>
#include "led.h"

#define BLINK_INTERVAL_MS 1000

int main(void){
  init_led();

  while(1){
    TOGGLE(LED_PORT, LED_PIN);
    _delay_ms(BLINK_INTERVAL_MS);
  }
}
