#ifndef ARDUBOY_TONES_H
#define ARDUBOY_TONES_H

// ArduboyTones library interface: one speaker, no volume control
// (TONE_HIGH_VOLUME is ignored), durations are in 1/1024 s

#include "Arduboy.h"

#define TONES_MAX_DURATION 0xFFFF

class ArduboyTones {
  public:
    // outEn: sound is played only if it returns true
    ArduboyTones(bool (*outEn)(void)) : out_enabled(outEn) {}

    void tone(uint16_t freq, uint16_t dur = 0){
      if(out_enabled()) arduboy_tone(freq & ~TONE_HIGH_VOLUME, ((uint32_t)dur * 1000) >> 10);
    }
    // tones: frequency, duration, ..., TONES_END (or TONES_REPEAT) in flash
    void tones(const uint16_t *tones){
      if(out_enabled()) arduboy_tones(tones, false);
    }
    void tonesInRAM(uint16_t *tones){
      if(out_enabled()) arduboy_tones(tones, true);
    }
    void noTone(void){ arduboy_no_tone(); }
    bool playing(void){ return arduboy_tone_playing(); }
    void volumeMode(uint8_t mode){}

  private:
    bool (*out_enabled)(void);
};

#endif
