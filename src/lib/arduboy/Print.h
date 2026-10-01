#ifndef ARDUBOY_PRINT_H
#define ARDUBOY_PRINT_H

// Arduino Print class: a derived class writes one char, Print formats

#include "Arduino.h"

class Print {
  public:
    virtual size_t write(uint8_t c) = 0;

    size_t print(const char *str){
      size_t n = 0;
      while(*str) n += write(*str++);
      return n;
    }
    size_t print(const __FlashStringHelper *str){
      const char *p = reinterpret_cast<const char *>(str);
      size_t n = 0;
      char c;
      while((c = pgm_read_byte(p++))) n += write(c);
      return n;
    }
    size_t print(char c){ return write(c); }
    // as Arduino: unsigned char is printed as a number
    size_t print(unsigned char n){ return printNumber(n); }
    size_t print(int n){ return print((long)n); }
    size_t print(unsigned int n){ return printNumber(n); }
    size_t print(long n){
      if(n < 0) return write('-') + printNumber(-n);
      return printNumber(n);
    }
    size_t print(unsigned long n){ return printNumber(n); }

    size_t println(void){ return write('\r') + write('\n'); }
    template<typename T> size_t println(T value){ return print(value) + println(); }

  private:
    size_t printNumber(unsigned long n){
      char digits[10];
      uint8_t len = 0;
      do{
        digits[len++] = '0' + n % 10;
        n /= 10;
      }while(n);
      for(uint8_t i = len; i > 0; i--) write(digits[i - 1]);
      return len;
    }
};

#endif
