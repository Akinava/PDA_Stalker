#ifndef ARDUBOY_TONES_FX_H
#define ARDUBOY_TONES_FX_H

// ArduboyTonesFX library interface: ArduboyTones + sequences from FX data.
// FX sequence is read to the ring buffer by fillBufferFromFX (call it every
// frame), the timer interrupt plays it from the buffer: the card is not read
// in the interrupt. One speaker, no volume control.

#include "Arduboy.h"
#include "ArduboyFX.h"
#include "ArduboyTonesPitches.h"

#define VOLUME_IN_TONE          0
#define VOLUME_ALWAYS_NORMAL    1
#define VOLUME_ALWAYS_HIGH      2

class ArduboyTonesFX {
  public:
    // outEn: sound is played only if it returns true
    ArduboyTonesFX(bool (*outEn)(void));
    ArduboyTonesFX(bool (*outEn)(void), uint16_t *tonesBufferFX, uint8_t tonesBufferLen);
    template<size_t size>
    ArduboyTonesFX(bool (*outEn)(void), uint16_t (&buffer)[size]) :
      ArduboyTonesFX(outEn, buffer, size)
    {
      static_assert(size < 128, "Buffer too large");
    }

    // durations are in 1/1024 s
    void tone(uint16_t freq, uint16_t dur = 0);
    void tone(uint16_t freq1, uint16_t dur1, uint16_t freq2, uint16_t dur2);
    void tone(uint16_t freq1, uint16_t dur1, uint16_t freq2, uint16_t dur2,
              uint16_t freq3, uint16_t dur3);
    void tones(const uint16_t *tones);
    void tonesInRAM(uint16_t *tones);
    void tonesFromFX(uint24_t tones);
    void fillBufferFromFX(void);
    void noTone(void);
    void volumeMode(uint8_t mode){}
    bool playing(void);

  private:
    bool (*out_enabled)(void);
    uint16_t sequence[7];
};

#endif
