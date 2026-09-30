#include "pins.h"
#include "macro.h"

#ifndef MIC_H
#define MIC_H

// ADC channel number is the same as the pin number of port C (PC1 -> ADC1)
#define MIC_ADC_CHANNEL         MICROPHONE_PIN
#define MIC_LEVEL_MAX           1023    // 10 bit ADC
#define MIC_LEVEL_SAMPLES       128     // ~13 ms at ADC clock fosc/128

void init_mic(void);
uint16_t mic_read(void);
uint16_t mic_get_level(uint8_t samples);

#endif
