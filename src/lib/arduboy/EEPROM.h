#ifndef ARDUBOY_EEPROM_H
#define ARDUBOY_EEPROM_H

#include <avr/eeprom.h>
#include "Arduino.h"

// Arduino EEPROM library interface
struct EEPROMClass {
  uint8_t read(int address){
    return eeprom_read_byte((const uint8_t *)address);
  }
  // only changed bytes are written: EEPROM has limited write cycles
  void write(int address, uint8_t value){
    eeprom_update_byte((uint8_t *)address, value);
  }
  void update(int address, uint8_t value){
    eeprom_update_byte((uint8_t *)address, value);
  }
};

static EEPROMClass EEPROM;

#endif
