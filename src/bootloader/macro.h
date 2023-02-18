#ifndef IO_H                                            
#define IO_H

// Set pin direction
#define SET_DDR_OUT(DDR, PIN) DDR |= _BV(PIN)                                   
#define SET_DDR_IN(DDR, PIN) DDR &= ~ _BV(PIN)                                  
#define SET_PULLUP(PORTS, PIN) PORTS |= _BV(PIN)                                
                                                                                 
// Set pin level
#define SET_HIGH(PORTS, PIN) PORTS |= _BV(PIN)                                  
#define SET_LOW(PORTS, PIN) PORTS &= ~ _BV(PIN)                                 
#define TOGGLE(PORTS, PIN) PORTS ^= _BV(PIN);                                   
                                                                               
// Check pullup pin level
#define CHECK_PIN(PINS, PIN) !(PINS & _BV(PIN))                                 

#endif
