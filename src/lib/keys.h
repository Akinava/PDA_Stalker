#include "pins.h"
#include "macro.h"
#include "menu.h"

#ifndef KEYS_H
#define KEYS_H

void init_keys(void);
uint8_t keys_read(void);
uint8_t keys_get_press(void);

// held arrow key repeats after the delay with the rate, A, B, C do not
#define KEYS_REPEAT_DELAY_MS 400
#define KEYS_REPEAT_RATE_MS  100
uint8_t keys_get_repeat(uint8_t interval_ms);

#endif