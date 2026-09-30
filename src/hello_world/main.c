#include <util/delay.h>
#include "led.h"
#include "keys.h"
#include "loader.h"

#define BLINK_INTERVAL_MS 1000
// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20

int main(void){
  uint16_t elapsed_ms = 0;

  init_led();
  init_keys();

  while(1){
    // C: back to the default app (file manager)
    if(keys_get_press() == C_KEY_PRESSED && loader_is_present()){
      loader_load_default_app();
    }
    if(elapsed_ms >= BLINK_INTERVAL_MS){
      TOGGLE(LED_PORT, LED_PIN);
      elapsed_ms = 0;
    }
    _delay_ms(POLL_INTERVAL_MS);
    elapsed_ms += POLL_INTERVAL_MS;
  }
}
