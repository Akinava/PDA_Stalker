#ifndef ARDUBOY_H
#define ARDUBOY_H

// Arduboy library interface (v1.1) on the PDA hardware:
// ST7920 128x64 instead of SSD1306, keys, speaker and LED of the PDA.
// Frame buffer has the Arduboy layout: 8 pages of 128 bytes,
// one byte is a column of 8 pixels, bit 0 is the top pixel.
// C key opens "EXIT APP?" window (keys polling and nextFrame),
// C again loads the file manager

#include "Arduino.h"
// as in Arduboy library, games use EEPROM without include
#include "EEPROM.h"

#define WIDTH   128
#define HEIGHT  64

#define BLACK   0
#define WHITE   1
#define INVERT  2

// buttons bits, the same as on Arduboy
#define LEFT_BUTTON     _BV(5)
#define RIGHT_BUTTON    _BV(6)
#define UP_BUTTON       _BV(7)
#define DOWN_BUTTON     _BV(4)
#define A_BUTTON        _BV(3)
#define B_BUTTON        _BV(2)

#define EEPROM_STORAGE_SPACE_START 16

#define ARDUBOY_BUFFER_SIZE (WIDTH * HEIGHT / 8)

// tone player of the speaker (Timer2 counts the durations)
// sequence: frequency, duration in 1/1024 s, ..., TONES_END (ArduboyTones format)
#define TONE_HIGH_VOLUME    0x8000
#define TONES_END           0x8000
#define TONES_REPEAT        0x8001
void arduboy_tone(unsigned int freq, unsigned long duration_ms);
void arduboy_tones(const uint16_t *tones, bool in_ram);
void arduboy_no_tone(void);
bool arduboy_tone_playing(void);

// EEPROM byte of the sound on / off state, the same as in Arduboy and Arduboy2
#define EEPROM_AUDIO_ON_OFF 2

class ArduboyAudio {
  public:
    // fresh EEPROM (0xFF) is "on"
    static void begin(void){
      audio_enabled = EEPROM.read(EEPROM_AUDIO_ON_OFF) != 0;
    }
    static void on(void){ audio_enabled = true; }
    static void off(void){
      audio_enabled = false;
      arduboy_no_tone();
    }
    static void toggle(void){
      if(audio_enabled) off(); else on();
    }
    static void saveOnOff(void){
      EEPROM.update(EEPROM_AUDIO_ON_OFF, audio_enabled);
    }
    static bool enabled(void){ return audio_enabled; }

  private:
    static bool audio_enabled;
};

class ArduboyTunes {
  public:
    // square wave freq Hz for duration ms (0 - until noTone), only if audio is on
    void tone(unsigned int freq, unsigned long duration);
    void noTone(void);
};

class Arduboy {
  public:
    ArduboyTunes tunes;
    ArduboyAudio audio;

    void begin(void);
    void beginNoLogo(void);
    void boot(void);

    uint8_t getInput(void);
    uint8_t buttonsState(void);
    bool pressed(uint8_t buttons);
    bool notPressed(uint8_t buttons);

    void setFrameRate(uint8_t rate);
    bool nextFrame(void);
    bool everyXFrames(uint8_t frames);

    void initRandomSeed(void);
    void setRGBled(uint8_t red, uint8_t green, uint8_t blue);
    void invert(bool inverse);

    uint8_t *getBuffer(void);
    void clear(void);
    void fillScreen(uint8_t color);
    void display(void);
    void paintScreen(const uint8_t *image);

    void drawPixel(int16_t x, int16_t y, uint8_t color = WHITE);
    uint8_t getPixel(uint8_t x, uint8_t y);
    void drawFastHLine(int16_t x, int16_t y, uint8_t w, uint8_t color = WHITE);
    void drawFastVLine(int16_t x, int16_t y, uint8_t h, uint8_t color = WHITE);
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color = WHITE);
    void drawRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color = WHITE);
    void fillRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color = WHITE);
    void drawCircle(int16_t x0, int16_t y0, uint8_t r, uint8_t color = WHITE);
    void fillCircle(int16_t x0, int16_t y0, uint8_t r, uint8_t color = WHITE);
    // Arduboy bitmap: vertical bytes, (h + 7) / 8 rows of w bytes
    void drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t w, uint8_t h, uint8_t color = WHITE);

    // text: 5x7 font in 6x8 cells, the cell background is cleared
    void setCursor(int16_t x, int16_t y);
    void setTextWrap(bool wrap);
    void setTextSize(uint8_t size);
    void drawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg, uint8_t size = 1);
    size_t write(uint8_t c);
    size_t print(char c);
    size_t print(const char *str);
    size_t print(const __FlashStringHelper *str);
    size_t print(unsigned char n);
    size_t print(int n);
    size_t print(unsigned int n);
    size_t print(long n);
    size_t print(unsigned long n);

  protected:
    friend class Sprites;
    static uint8_t sBuffer[ARDUBOY_BUFFER_SIZE];

  private:
    uint8_t frameCount;
    uint8_t eachFrameMillis;
    unsigned long nextFrameStart;
    int16_t cursorX;
    int16_t cursorY;
    uint8_t textSize;
    bool textWrap;
    bool inverted;

    size_t printNumber(unsigned long n);
    void checkExit(void);
    void exitMenu(void);
    void paintRows(const uint8_t *image, uint8_t blank_top, uint8_t blank_bottom);
};

#endif
