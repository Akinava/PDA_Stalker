#include "pins.h"
#include "macro.h"
#include "menu.h"

#ifndef KEYS_H
#define KEYS_H

void init_keys(void);
uint8_t keys_read(void);
uint8_t keys_get_press(void);

#endif