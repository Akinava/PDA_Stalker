#ifndef ARDUBOY2_H
#define ARDUBOY2_H

// Arduboy2 library interface on top of the Arduboy (v1.1) one, see Arduboy.h

#include "Arduboy.h"
#include "Sprites.h"

#define RGB_ON  1
#define RGB_OFF 0

// EEPROM byte of the sound on / off state, the same as in Arduboy2
#define EEPROM_AUDIO_ON_OFF 2

class Arduboy2Audio {
  public:
    static void begin(void){
      audio_enabled = EEPROM.read(EEPROM_AUDIO_ON_OFF) != 0;
    }
    static void on(void){ audio_enabled = true; }
    static void off(void){
      audio_enabled = false;
      arduboy_no_tone();
    }
    static void toggle(void){
      if(audio_enabled) off(); else on();
    }
    static void saveOnOff(void){
      EEPROM.update(EEPROM_AUDIO_ON_OFF, audio_enabled);
    }
    static bool enabled(void){ return audio_enabled; }

  private:
    static bool audio_enabled;
};

class Arduboy2Base : public Arduboy {
  public:
    Arduboy2Audio audio;

    void begin(void){
      Arduboy::begin();
      audio.begin();
    }
    void boot(void){ begin(); }

    // boot time functions of Arduboy2 have no PDA equivalent
    void flashlight(void){}
    void systemButtons(void){}
    void bootLogo(void){}

    using Arduboy::display;
    void display(bool clear){
      Arduboy::display();
      if(clear) Arduboy::clear();
    }

    void digitalWriteRGB(uint8_t red, uint8_t green, uint8_t blue){
      setRGBled(red == RGB_ON, green == RGB_ON, blue == RGB_ON);
    }

    unsigned long generateRandomSeed(void){
      initRandomSeed();
      return rand();
    }
};

class Arduboy2 : public Arduboy2Base {};

#endif
