#include "keys.h"

void init_keys(void){
  SET_DDR_IN(BUTTON_A_DDR, BUTTON_A_PIN);
  SET_PULLUP(BUTTON_A_PORT, BUTTON_A_PIN);
  SET_DDR_IN(BUTTON_B_DDR, BUTTON_B_PIN);
  SET_PULLUP(BUTTON_B_PORT, BUTTON_B_PIN);
  SET_DDR_IN(BUTTON_C_DDR, BUTTON_C_PIN);
  SET_PULLUP(BUTTON_C_PORT, BUTTON_C_PIN);
  SET_DDR_IN(BUTTON_UP_DDR, BUTTON_UP_PIN);
  SET_PULLUP(BUTTON_UP_PORT, BUTTON_UP_PIN);
  SET_DDR_IN(BUTTON_DOWN_DDR, BUTTON_DOWN_PIN);
  SET_PULLUP(BUTTON_DOWN_PORT, BUTTON_DOWN_PIN);
  SET_DDR_IN(BUTTON_LEFT_DDR, BUTTON_LEFT_PIN);
  SET_PULLUP(BUTTON_LEFT_PORT, BUTTON_LEFT_PIN);
  SET_DDR_IN(BUTTON_RIGHT_DDR, BUTTON_RIGHT_PIN);
  SET_PULLUP(BUTTON_RIGHT_PORT, BUTTON_RIGHT_PIN);
}

// code of the pressed key (see menu.h), NOOP if nothing is pressed
uint8_t keys_read(void){
  if(CHECK_PIN(BUTTON_A_PINS, BUTTON_A_PIN)) return A_KEY_PRESSED;
  if(CHECK_PIN(BUTTON_B_PINS, BUTTON_B_PIN)) return B_KEY_PRESSED;
  if(CHECK_PIN(BUTTON_C_PINS, BUTTON_C_PIN)) return C_KEY_PRESSED;
  if(CHECK_PIN(BUTTON_UP_PINS, BUTTON_UP_PIN)) return UP_KEY_PRESSED;
  if(CHECK_PIN(BUTTON_DOWN_PINS, BUTTON_DOWN_PIN)) return DOWN_KEY_PRESSED;
  if(CHECK_PIN(BUTTON_LEFT_PINS, BUTTON_LEFT_PIN)) return LEFT_KEY_PRESSED;
  if(CHECK_PIN(BUTTON_RIGHT_PINS, BUTTON_RIGHT_PIN)) return RIGHT_KEY_PRESSED;
  return NOOP;
}

// code of the key only at the moment it is pressed, NOOP otherwise.
// call it periodically with interval > contact bounce time (~20 ms)
uint8_t keys_get_press(void){
  static uint8_t last_key = NOOP;
  uint8_t key = keys_read();
  if(key == last_key) return NOOP;
  last_key = key;
  return key;
}
