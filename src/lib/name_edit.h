#include <avr/pgmspace.h>
#include "pins.h"
#include "macro.h"
#include "fat16_edit.h"

#ifndef NAME_EDIT_H
#define NAME_EDIT_H

// text mode editor of 8.3 name: title is in flash, raw (8 + 3 chars with
// spaces) is the start name and the result.
// UP / DOWN - char, LEFT / RIGHT - position, A - ok, C - cancel.
// returns 1 on A with not empty name
uint8_t name_edit(const char *title, uint8_t *raw);

#endif
