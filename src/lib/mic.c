#include "mic.h"

void init_mic(void){
  SET_DDR_IN(MICROPHONE_DDR, MICROPHONE_PIN);
  // no pull-up: it shifts the analog signal
  SET_LOW(MICROPHONE_PORT, MICROPHONE_PIN);
  // disable digital input buffer on the analog pin
  SET(DIDR0, MIC_ADC_CHANNEL);

  // reference: external AREF (AREF is connected to VCC), right adjust result
  ADMUX = MIC_ADC_CHANNEL & 0x0F;

  // enable ADC, prescaler fosc/128: 16 MHz / 128 = 125 kHz ADC clock
  ADCSRA = _BV(ADEN)|_BV(ADPS2)|_BV(ADPS1)|_BV(ADPS0);

  // first conversion is longer and less accurate, drop it
  mic_read();
}

// one ADC sample 0..1023, ~104 us
uint16_t mic_read(void){
  SET(ADCSRA, ADSC);
  while(ADCSRA & _BV(ADSC));
  return ADC;
}

// sound level: peak-to-peak amplitude 0..MIC_LEVEL_MAX over samples
uint16_t mic_get_level(uint8_t samples){
  uint16_t min = MIC_LEVEL_MAX;
  uint16_t max = 0;
  while(samples--){
    uint16_t value = mic_read();
    if(value < min) min = value;
    if(value > max) max = value;
  }
  return max > min ? max - min : 0;
}
