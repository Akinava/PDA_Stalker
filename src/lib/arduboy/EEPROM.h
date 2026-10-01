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
  template<typename T> T &get(int address, T &value){
    eeprom_read_block(&value, (const void *)address, sizeof(T));
    return value;
  }
  template<typename T> const T &put(int address, const T &value){
    eeprom_update_block(&value, (void *)address, sizeof(T));
    return value;
  }
};

static EEPROMClass EEPROM;

#endif
