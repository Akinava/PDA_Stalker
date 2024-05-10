#include "led.h"

void init_led(void){
  SET_DDR_OUT(LED_DDR, LED_PIN);
}