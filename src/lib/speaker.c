#include "speaker.h"

void init_speaker(void){
  SET_DDR_OUT(SPEAKER_DDR, SPEAKER_PIN);
  speaker_off();
}

// square wave freq Hz on the speaker pin:
// freq = F_CPU / (2 * prescaler * (1 + OCR0A))
void speaker_tone(uint16_t freq){
  static const uint16_t prescalers[] = {1, 8, 64, 256, 1024};

  if(freq < SPEAKER_FREQ_MIN){
    speaker_off();
    return;
  }

  // the smallest prescaler gives the most accurate frequency
  uint8_t cs = 0;
  uint32_t top;
  do{
    top = F_CPU / (2UL * prescalers[cs] * freq) - 1;
    cs++;
  }while(top > 0xFF && cs < sizeof(prescalers) / sizeof(prescalers[0]));

  OCR0A = top;
  OCR0B = 0;
  // toggle OC0B on compare match, CTC mode (TOP = OCR0A)
  TCCR0A = _BV(COM0B0)|_BV(WGM01);
  // CS02..CS00 = 1..5 is prescaler 1..1024
  TCCR0B = cs;
}

void speaker_off(void){
  TCCR0B = 0;
  TCCR0A = 0;
  // transistor closed: no current through the speaker
  SET_LOW(SPEAKER_PORT, SPEAKER_PIN);
}
