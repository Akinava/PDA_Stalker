#ifndef ARDUBOY_TONES_H
#define ARDUBOY_TONES_H

// ArduboyTones library interface: one speaker, no volume control
// (TONE_HIGH_VOLUME is ignored), durations are in 1/1024 s

#include "Arduboy.h"
#include "ArduboyTonesPitches.h"

#define TONES_MAX_DURATION 0xFFFF

class ArduboyTones {
  public:
    // outEn: sound is played only if it returns true
    ArduboyTones(bool (*outEn)(void)) : out_enabled(outEn) {}

    void tone(uint16_t freq, uint16_t dur = 0){
      if(out_enabled()) arduboy_tone(freq & ~TONE_HIGH_VOLUME, ((uint32_t)dur * 1000) >> 10);
    }
    // two or three tones one after another
    // the playing sequence is stopped before it is overwritten
    void tone(uint16_t freq1, uint16_t dur1, uint16_t freq2, uint16_t dur2){
      arduboy_no_tone();
      sequence[0] = freq1; sequence[1] = dur1;
      sequence[2] = freq2; sequence[3] = dur2;
      sequence[4] = TONES_END;
      tonesInRAM(sequence);
    }
    void tone(uint16_t freq1, uint16_t dur1, uint16_t freq2, uint16_t dur2,
              uint16_t freq3, uint16_t dur3){
      arduboy_no_tone();
      sequence[0] = freq1; sequence[1] = dur1;
      sequence[2] = freq2; sequence[3] = dur2;
      sequence[4] = freq3; sequence[5] = dur3;
      sequence[6] = TONES_END;
      tonesInRAM(sequence);
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
    uint16_t sequence[7];
};

#endif
