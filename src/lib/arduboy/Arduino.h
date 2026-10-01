#ifndef ARDUBOY_ARDUINO_H
#define ARDUBOY_ARDUINO_H

// minimal Arduino core for games ported from Arduboy (C++ only)

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include "binary.h"

typedef bool boolean;
typedef uint8_t byte;

// as Arduino: F("text") is in flash, print() has an overload for it
class __FlashStringHelper;
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))

#define F(str) (reinterpret_cast<const __FlashStringHelper *>(PSTR(str)))

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

// Timer2 counts milliseconds (Timer0 is the speaker tone)
unsigned long millis(void);
void delay(unsigned long ms);

// as Arduino: avr-libc random(), randomSeed() seeds it
static inline void randomSeed(unsigned long seed){
  if(seed) srandom(seed);
}

static inline long random(long max_value){
  return max_value ? random() % max_value : 0;
}

static inline long random(long min_value, long max_value){
  return min_value < max_value ? min_value + random(max_value - min_value) : min_value;
}

#endif
