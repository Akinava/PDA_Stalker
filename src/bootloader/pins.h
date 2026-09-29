#include <avr/io.h>

#ifndef PINS_H
#define PINS_H
                                                                                
// DEFINE BUTTON C
#define BUTTON_C_DDR DDRD
#define BUTTON_C_PORT PORTD
#define BUTTON_C_PINS PIND
#define BUTTON_C_PIN PD2

// DEFINE LED
#define LED_DDR DDRD
#define LED_PORT PORTD
#define LED_PIN PD0

// SD
#define SD_DDR DDRB
#define SD_PORT PORTB
#define SD_CS PB2
#define MOSI PB3
#define MISO PB4
#define SCK PB5

#endif
