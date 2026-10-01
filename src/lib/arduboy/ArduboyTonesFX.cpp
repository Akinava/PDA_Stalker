#include "ArduboyTonesFX.h"

// ring buffer of FX sequence: head - next write (main loop), tail - next
// read (timer interrupt), tail -1: the buffer is empty before the first fill
static volatile uint16_t *ring;
static uint8_t ring_len;
static volatile uint8_t ring_head;
static volatile int8_t ring_tail;
static uint24_t fx_start;
static uint24_t fx_current;
static bool fx_mode;

// called from the timer interrupt
static uint16_t ring_next(void){
  uint16_t value = ring[ring_tail];
  ring_tail = (ring_tail + 1) % ring_len;
  return value;
}

ArduboyTonesFX::ArduboyTonesFX(bool (*outEn)(void)) : out_enabled(outEn) {}

ArduboyTonesFX::ArduboyTonesFX(bool (*outEn)(void), uint16_t *tonesBufferFX, uint8_t tonesBufferLen)
  : out_enabled(outEn)
{
  ring = tonesBufferFX;
  ring_len = tonesBufferLen;
}

void ArduboyTonesFX::tone(uint16_t freq, uint16_t dur){
  fx_mode = false;
  if(out_enabled()) arduboy_tone(freq & ~TONE_HIGH_VOLUME, ((uint32_t)dur * 1000) >> 10);
}

// the playing sequence is stopped before it is overwritten
void ArduboyTonesFX::tone(uint16_t freq1, uint16_t dur1, uint16_t freq2, uint16_t dur2){
  noTone();
  sequence[0] = freq1; sequence[1] = dur1;
  sequence[2] = freq2; sequence[3] = dur2;
  sequence[4] = TONES_END;
  tonesInRAM(sequence);
}

void ArduboyTonesFX::tone(uint16_t freq1, uint16_t dur1, uint16_t freq2, uint16_t dur2,
                          uint16_t freq3, uint16_t dur3){
  noTone();
  sequence[0] = freq1; sequence[1] = dur1;
  sequence[2] = freq2; sequence[3] = dur2;
  sequence[4] = freq3; sequence[5] = dur3;
  sequence[6] = TONES_END;
  tonesInRAM(sequence);
}

void ArduboyTonesFX::tones(const uint16_t *tones){
  fx_mode = false;
  if(out_enabled()) arduboy_tones(tones, false);
}

void ArduboyTonesFX::tonesInRAM(uint16_t *tones){
  fx_mode = false;
  if(out_enabled()) arduboy_tones(tones, true);
}

void ArduboyTonesFX::tonesFromFX(uint24_t tones){
  if(!ring_len || !out_enabled()) return;
  arduboy_no_tone();
  fx_start = fx_current = tones;
  ring_head = 0;
  ring_tail = -1;
  fx_mode = true;
  fillBufferFromFX();
  arduboy_tones_source(ring_next);
}

// as ArduboyTonesFX: the buffer is filled up to the read position
void ArduboyTonesFX::fillBufferFromFX(void){
  if(!fx_mode || ring_head == ring_tail) return;
  if(ring_tail != -1 && !arduboy_tone_playing()) return;

  uint8_t head = ring_head;
  FX::seekData(fx_current);
  while((head % ring_len) != ring_tail){
    uint16_t t = FX::readPendingUInt16();
    ring[head % ring_len] = t;
    head++;
    fx_current += 2;

    if(t == TONES_REPEAT){
      fx_current = fx_start;
      FX::seekData(fx_current);
    }
    if(ring_tail == -1) ring_tail = 0;
  }
  ring_head = head % ring_len;
}

void ArduboyTonesFX::noTone(void){
  fx_mode = false;
  arduboy_no_tone();
}

bool ArduboyTonesFX::playing(void){
  return arduboy_tone_playing();
}
