#ifndef ARDUBOY2_H
#define ARDUBOY2_H

// Arduboy2 library interface on top of the Arduboy (v1.1) one, see Arduboy.h

#include "Arduboy.h"
#include "Sprites.h"

#define RGB_ON  1
#define RGB_OFF 0

typedef ArduboyAudio Arduboy2Audio;

class Arduboy2Base : public Arduboy {
  public:
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
