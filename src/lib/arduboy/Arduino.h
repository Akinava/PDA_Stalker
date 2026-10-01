#ifndef ARDUBOY_ARDUINO_H
#define ARDUBOY_ARDUINO_H

// minimal Arduino core for games ported from Arduboy (C++ only)

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include "binary.h"

typedef bool boolean;
typedef uint8_t byte;

#define F(str) (str)

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

// Timer2 counts milliseconds (Timer0 is the speaker tone)
unsigned long millis(void);
void delay(unsigned long ms);

static inline long random(long max_value){
  return max_value ? rand() % max_value : 0;
}

static inline long random(long min_value, long max_value){
  return min_value + random(max_value - min_value);
}

#endif
