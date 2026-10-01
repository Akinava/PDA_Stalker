#include <util/delay.h>
#include <util/atomic.h>
#include "Arduboy.h"

// libraries of the PDA are C
extern "C" {
#include "pins.h"
#include "macro.h"
#include "spi.h"
#include "display.h"
#include "display_gfx.h"
#include "keys.h"
#include "led.h"
#include "speaker.h"
#include "radio.h"
#include "mic.h"
#include "loader.h"
}

// ST7920 graphics
#define LCD_EXTENDED_FUNCTION   0x34    // extended instruction set, graphics off
#define LCD_BASIC_FUNCTION      0x30
#define LCD_SET_ADDRESS         0x80    // vertical, then horizontal address
// GDRAM is 256x32: one GDRAM line is screen row y (left) and row y + 32 (right)
#define LCD_GDRAM_LINES         32
#define LCD_LINE_BYTES          (WIDTH / 8)

// SPI clock while drawing: fosc/16 = 1 MHz (ST7920 serial clock limit
// at 3.3 V is ~1.6 MHz), the libraries use fosc/64
#define LCD_SPI_FAST()          do{ \
    SPCR = (SPCR & ~(_BV(SPR1)|_BV(SPR0))) | _BV(SPR0); \
    SPSR &= ~_BV(SPI2X); \
  }while(0)

// millis: Timer2 CTC, 16 MHz / 64 / 250 = 1 kHz
#define TIMER2_TOP              249

#define LCD_GRAPHIC_ON          0x36    // extended instruction set, graphics on

#define EXIT_POLL_INTERVAL_MS   20
// the window is on text rows 1, 2 (8x16 font): screen rows 16..47
#define EXIT_TOP                16
#define EXIT_BOTTOM             48

bool ArduboyAudio::audio_enabled;

static volatile unsigned long timer_millis;

// tone player: one tone or a sequence of (frequency, duration) pairs
static volatile unsigned long tone_end;
static volatile bool tone_timed;
static const uint16_t * volatile tone_seq;
static const uint16_t *tone_seq_start;
static bool tone_seq_in_ram;
static uint16_t (*tone_source)(void);

void (*arduboy_bus_release)(void);

void arduboy_lcd_spi(void){
  LCD_SPI_FAST();
}

// GDRAM line checksums: the line is sent only if it is changed.
// -DARDUBOY_NO_LINE_HASH saves RAM for games that redraw the whole screen
#ifndef ARDUBOY_NO_LINE_HASH
static uint16_t line_hash[LCD_GDRAM_LINES];
static bool hash_valid;
#endif

static uint16_t tone_seq_read(void){
  if(tone_source) return tone_source();
  const uint16_t *p = tone_seq;
  tone_seq = p + 1;
  return tone_seq_in_ram ? *p : pgm_read_word(p);
}

// next tone of the sequence, called with interrupts disabled
static void tone_seq_next(void){
  uint16_t freq = tone_seq_read();
  if(freq == TONES_REPEAT){
    tone_seq = tone_seq_start;
    freq = tone_seq_read();
  }
  if(freq == TONES_END){
    tone_seq = 0;
    tone_source = 0;
    tone_timed = false;
    speaker_off();
    return;
  }
  // durations of ArduboyTones are in 1/1024 s
  uint16_t duration = tone_seq_read();
  speaker_tone(freq & ~TONE_HIGH_VOLUME);
  tone_end = timer_millis + ((uint32_t)duration * 1000 >> 10);
  tone_timed = duration != 0;
}

ISR(TIMER2_COMPA_vect){
  unsigned long ms = ++timer_millis;
  if(tone_timed && (long)(ms - tone_end) >= 0){
    if(tone_seq){
      tone_seq_next();
    }else{
      tone_timed = false;
      speaker_off();
    }
  }
}

unsigned long millis(void){
  unsigned long ms;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
    ms = timer_millis;
  }
  return ms;
}

void delay(unsigned long ms){
  unsigned long start = millis();
  while(millis() - start < ms);
}

static void init_timer(void){
  TCCR2A = _BV(WGM21);
  TCCR2B = _BV(CS22);   // clk / 64
  OCR2A = TIMER2_TOP;
  TIMSK2 = _BV(OCIE2A);
  sei();
}

/********************************** tones ************************************/

void arduboy_tone(unsigned int freq, unsigned long duration_ms){
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
    tone_seq = 0;
    tone_source = 0;
    tone_timed = false;
  }
  speaker_tone(freq);
  if(duration_ms){
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
      tone_end = timer_millis + duration_ms;
      tone_timed = true;
    }
  }
}

void arduboy_tones(const uint16_t *tones, bool in_ram){
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
    tone_source = 0;
    tone_seq_start = tones;
    tone_seq = tones;
    tone_seq_in_ram = in_ram;
    tone_seq_next();
  }
}

void arduboy_tones_source(uint16_t (*next)(void)){
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
    tone_source = next;
    // not 0: the sequence is playing, the pointer itself is not used
    tone_seq_start = tone_seq = (const uint16_t *)&tone_source;
    tone_seq_next();
  }
}

void arduboy_no_tone(void){
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
    tone_seq = 0;
    tone_source = 0;
    tone_timed = false;
  }
  speaker_off();
}

bool arduboy_tone_playing(void){
  return tone_timed || TCCR0B != 0;
}

void ArduboyTunes::tone(unsigned int freq, unsigned long duration){
  if(ArduboyAudio::enabled()) arduboy_tone(freq, duration);
}

void ArduboyTunes::noTone(void){
  arduboy_no_tone();
}

/*********************************** heap ************************************/

// as Arduino core (new.cpp): games use new / delete
void *operator new(size_t size){
  return malloc(size);
}

void *operator new[](size_t size){
  return malloc(size);
}

void operator delete(void *ptr){
  free(ptr);
}

void operator delete[](void *ptr){
  free(ptr);
}

void operator delete(void *ptr, size_t){
  free(ptr);
}

void operator delete[](void *ptr, size_t){
  free(ptr);
}

/********************************** system ***********************************/

void Arduboy::begin(void){
  init_keys();
  init_led();
  init_speaker();
  // CSN high before any SPI traffic, otherwise radio catches display data
  init_radio();
  init_display();
  display_gfx_on();
  LCD_SPI_FAST();
#ifndef ARDUBOY_NO_LINE_HASH
  hash_valid = false;
#endif
  init_timer();

  audio.begin();
  setFrameRate(60);
  textSize = 1;
  textWrap = false;
  inverted = false;
  clear();
}

void Arduboy::beginNoLogo(void){
  begin();
}

void Arduboy::boot(void){
  begin();
}

uint8_t Arduboy::getInput(void){
  checkExit();
  uint8_t buttons = 0;
  if(CHECK_PIN(BUTTON_LEFT_PINS, BUTTON_LEFT_PIN)) buttons |= LEFT_BUTTON;
  if(CHECK_PIN(BUTTON_RIGHT_PINS, BUTTON_RIGHT_PIN)) buttons |= RIGHT_BUTTON;
  if(CHECK_PIN(BUTTON_UP_PINS, BUTTON_UP_PIN)) buttons |= UP_BUTTON;
  if(CHECK_PIN(BUTTON_DOWN_PINS, BUTTON_DOWN_PIN)) buttons |= DOWN_BUTTON;
  if(CHECK_PIN(BUTTON_A_PINS, BUTTON_A_PIN)) buttons |= A_BUTTON;
  if(CHECK_PIN(BUTTON_B_PINS, BUTTON_B_PIN)) buttons |= B_BUTTON;
  return buttons;
}

uint8_t Arduboy::buttonsState(void){
  return getInput();
}

bool Arduboy::pressed(uint8_t buttons){
  return (getInput() & buttons) == buttons;
}

bool Arduboy::notPressed(uint8_t buttons){
  return (getInput() & buttons) == 0;
}

void Arduboy::pollButtons(void){
  previousButtonState = currentButtonState;
  currentButtonState = getInput();
}

bool Arduboy::justPressed(uint8_t button){
  return !(previousButtonState & button) && (currentButtonState & button);
}

bool Arduboy::justReleased(uint8_t button){
  return (previousButtonState & button) && !(currentButtonState & button);
}

void Arduboy::setFrameRate(uint8_t rate){
  eachFrameMillis = 1000 / rate;
}

// games poll keys in own loops (menus), so C is checked in getInput too
void Arduboy::checkExit(void){
  static bool c_was_pressed;
  bool c_pressed = CHECK_PIN(BUTTON_C_PINS, BUTTON_C_PIN);
  if(c_pressed && !c_was_pressed) exitMenu();
  c_was_pressed = c_pressed;
}

bool Arduboy::nextFrame(void){
  checkExit();

  unsigned long now = millis();
  if((long)(now - nextFrameStart) < 0) return false;
  nextFrameStart = now + eachFrameMillis;
  frameCount++;
  return true;
}

bool Arduboy::everyXFrames(uint8_t frames){
  return frameCount % frames == 0;
}

void Arduboy::initRandomSeed(void){
  // microphone noise in the low bits of ADC
  init_mic();
  unsigned long seed = millis();
  for(uint8_t i = 0; i < 32; i++){
    seed = (seed << 1) ^ mic_read();
  }
  seed ^= (seed >> 16) ^ TCNT2;
  srand((unsigned int)seed);
  randomSeed(seed);
}

// the PDA has one LED
void Arduboy::setRGBled(uint8_t red, uint8_t green, uint8_t blue){
  if(red | green | blue){
    SET_HIGH(LED_PORT, LED_PIN);
  }else{
    SET_LOW(LED_PORT, LED_PIN);
  }
}

// ST7920 can not invert graphics, so pixels are inverted while sending
void Arduboy::invert(bool inverse){
  inverted = inverse;
}

// modal "EXIT APP?" window: C loads the default app (file manager),
// any other key returns to the game. The window is the text layer of ST7920
// over blanked graphics: the frame buffer of the game is not changed
void Arduboy::exitMenu(void){
  uint8_t key;

  tunes.noTone();
  paintRows(sBuffer, EXIT_TOP, EXIT_BOTTOM);
  // basic instruction set: graphics stays on, the text is shown over it
  display_send_command(LCD_BASIC_FUNCTION);
  display_print_line_P(1, PSTR("   EXIT APP?"));
  display_print_line_P(2, PSTR("   C - YES"));
  LCD_SPI_FAST();

  while(keys_read() == C_KEY_PRESSED) _delay_ms(EXIT_POLL_INTERVAL_MS);
  while((key = keys_read()) == NOOP) _delay_ms(EXIT_POLL_INTERVAL_MS);

  if(key == C_KEY_PRESSED && loader_is_present()){
    display_print_line_P(1, PSTR("   loading..."));
    display_print_line_P(2, PSTR(""));
    // back to the text mode and the speed of the libraries
    display_send_command(LCD_EXTENDED_FUNCTION);
    display_send_command(LCD_BASIC_FUNCTION);
    init_spi();
    loader_load_default_app();
  }

  display_clear();
  display_send_command(LCD_EXTENDED_FUNCTION);
  display_send_command(LCD_GRAPHIC_ON);
  LCD_SPI_FAST();
  paintRows(sBuffer, 0, 0);

  // the key must not get into the game
  while(keys_read() != NOOP) _delay_ms(EXIT_POLL_INTERVAL_MS);
  nextFrameStart = millis();
}

/********************************** screen ***********************************/

uint8_t Arduboy::sBuffer[ARDUBOY_BUFFER_SIZE];
bool Arduboy::inverted;

uint8_t *Arduboy::getBuffer(void){
  return sBuffer;
}

void Arduboy::clear(void){
  fillScreen(BLACK);
}

void Arduboy::fillScreen(uint8_t color){
  memset(sBuffer, color == WHITE ? 0xFF : 0x00, sizeof(sBuffer));
}

void Arduboy::display(void){
  paintScreen(sBuffer);
}

// screen row y of the page buffer to the ST7920 line: high bit is the left pixel
static void convert_row(const uint8_t *image, uint8_t y, uint8_t *line, uint8_t invert_mask){
  const uint8_t *column = image + (y >> 3) * WIDTH;
  uint8_t mask = 1 << (y & 7);
  for(uint8_t i = 0; i < LCD_LINE_BYTES; i++){
    uint8_t byte = 0;
    for(uint8_t k = 0; k < 8; k++){
      byte <<= 1;
      if(*column++ & mask) byte |= 1;
    }
    line[i] = byte ^ invert_mask;
  }
}

void Arduboy::paintScreen(const uint8_t *image){
  paintRows(image, 0, 0);
}

// screen rows blank_top..blank_bottom - 1 are sent empty (for the exit window).
// The checksum is of the sent line, so blanked lines are restored by the next paint
void Arduboy::paintRows(const uint8_t *image, uint8_t blank_top, uint8_t blank_bottom){
  uint8_t line[2 * LCD_LINE_BYTES];
  uint8_t invert_mask = inverted ? 0xFF : 0x00;

  if(arduboy_bus_release) arduboy_bus_release();
#ifdef ARDUBOY_LCD_RESYNC
  // garbage of the other devices on the bus can get into the display:
  // graphics mode is set again on every frame
  display_send_command(LCD_GRAPHIC_ON);
#endif

  for(uint8_t y = 0; y < LCD_GDRAM_LINES; y++){
    for(uint8_t half = 0; half < 2; half++){
      uint8_t row = y + half * LCD_GDRAM_LINES;
      if(row >= blank_top && row < blank_bottom){
        memset(line + half * LCD_LINE_BYTES, 0, LCD_LINE_BYTES);
      }else{
        convert_row(image, row, line + half * LCD_LINE_BYTES, invert_mask);
      }
    }

#ifndef ARDUBOY_NO_LINE_HASH
    // Fletcher-16
    uint8_t sum1 = 0, sum2 = 0;
    for(uint8_t i = 0; i < sizeof(line); i++){
      sum1 += line[i];
      sum2 += sum1;
    }
    uint16_t hash = (sum2 << 8) | sum1;
    if(hash_valid && line_hash[y] == hash) continue;
    line_hash[y] = hash;
#endif

    display_send_command(LCD_SET_ADDRESS | y);
    display_send_command(LCD_SET_ADDRESS | 0);
    // the data bytes go after one sync byte while CS is high
    spi_set_mode(SPI_MODE3);
    SET_HIGH(LCD_PORT, LCD_CS);
    spi_send(DISPLAY_SYNC_DATA);
    for(uint8_t i = 0; i < sizeof(line); i++){
      spi_send(line[i] & 0xF0);
      spi_send(line[i] << 4);
    }
    SET_LOW(LCD_PORT, LCD_CS);
  }
#ifndef ARDUBOY_NO_LINE_HASH
  hash_valid = true;
#endif
}

/********************************** drawing **********************************/

void Arduboy::drawPixel(int16_t x, int16_t y, uint8_t color){
  if(x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
  uint8_t *byte = &sBuffer[(y >> 3) * WIDTH + x];
  uint8_t mask = 1 << (y & 7);
  if(color == WHITE){
    *byte |= mask;
  }else if(color == BLACK){
    *byte &= ~mask;
  }else{
    *byte ^= mask;
  }
}

uint8_t Arduboy::getPixel(uint8_t x, uint8_t y){
  if(x >= WIDTH || y >= HEIGHT) return BLACK;
  return (sBuffer[(y >> 3) * WIDTH + x] >> (y & 7)) & 1;
}

void Arduboy::drawFastHLine(int16_t x, int16_t y, uint8_t w, uint8_t color){
  for(int16_t end = x + w; x < end; x++) drawPixel(x, y, color);
}

void Arduboy::drawFastVLine(int16_t x, int16_t y, uint8_t h, uint8_t color){
  for(int16_t end = y + h; y < end; y++) drawPixel(x, y, color);
}

void Arduboy::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color){
  int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int16_t err = dx + dy;
  while(1){
    drawPixel(x0, y0, color);
    if(x0 == x1 && y0 == y1) break;
    int16_t e2 = 2 * err;
    if(e2 >= dy){ err += dy; x0 += sx; }
    if(e2 <= dx){ err += dx; y0 += sy; }
  }
}

void Arduboy::drawRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color){
  if(!w || !h) return;
  drawFastHLine(x, y, w, color);
  drawFastHLine(x, y + h - 1, w, color);
  drawFastVLine(x, y, h, color);
  drawFastVLine(x + w - 1, y, h, color);
}

void Arduboy::fillRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color){
  for(int16_t end = x + w; x < end; x++) drawFastVLine(x, y, h, color);
}

void Arduboy::drawCircle(int16_t x0, int16_t y0, uint8_t r, uint8_t color){
  int16_t f = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x = 0;
  int16_t y = r;

  drawPixel(x0, y0 + r, color);
  drawPixel(x0, y0 - r, color);
  drawPixel(x0 + r, y0, color);
  drawPixel(x0 - r, y0, color);

  while(x < y){
    if(f >= 0){
      y--;
      ddF_y += 2;
      f += ddF_y;
    }
    x++;
    ddF_x += 2;
    f += ddF_x;

    drawPixel(x0 + x, y0 + y, color);
    drawPixel(x0 - x, y0 + y, color);
    drawPixel(x0 + x, y0 - y, color);
    drawPixel(x0 - x, y0 - y, color);
    drawPixel(x0 + y, y0 + x, color);
    drawPixel(x0 - y, y0 + x, color);
    drawPixel(x0 + y, y0 - x, color);
    drawPixel(x0 - y, y0 - x, color);
  }
}

void Arduboy::fillCircle(int16_t x0, int16_t y0, uint8_t r, uint8_t color){
  int16_t f = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x = 0;
  int16_t y = r;

  drawFastVLine(x0, y0 - r, 2 * r + 1, color);
  while(x < y){
    if(f >= 0){
      y--;
      ddF_y += 2;
      f += ddF_y;
    }
    x++;
    ddF_x += 2;
    f += ddF_x;

    drawFastVLine(x0 + x, y0 - y, 2 * y + 1, color);
    drawFastVLine(x0 - x, y0 - y, 2 * y + 1, color);
    drawFastVLine(x0 + y, y0 - x, 2 * x + 1, color);
    drawFastVLine(x0 - y, y0 - x, 2 * x + 1, color);
  }
}

void Arduboy::drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t w, uint8_t h, uint8_t color){
  for(uint8_t j = 0; j < h; j++){
    for(uint8_t i = 0; i < w; i++){
      if((pgm_read_byte(bitmap + (j >> 3) * w + i) >> (j & 7)) & 1){
        drawPixel(x + i, y + j, color);
      }
    }
  }
}

// bit reader of drawCompressed: bits from the low one
struct CompressedReader {
  const uint8_t *src;
  uint16_t bit;
  uint8_t byte;

  uint16_t get(uint8_t bits){
    uint16_t value = 0;
    for(uint8_t i = 0; i < bits; i++){
      if(bit == 0x100){
        bit = 1;
        byte = pgm_read_byte(src++);
      }
      if(byte & bit) value |= 1 << i;
      bit <<= 1;
    }
    return value;
  }
};

// the same algorithm as Arduboy2Base::drawCompressed
void Arduboy::drawCompressed(int16_t sx, int16_t sy, const uint8_t *bitmap, uint8_t color){
  CompressedReader cs = {bitmap, 0x100, 0};

  int16_t w = cs.get(8) + 1;
  int16_t h = cs.get(8) + 1;
  uint8_t col = cs.get(1);    // starting colour

  if(sx + w < 0 || sx > WIDTH - 1 || sy + h < 0 || sy > HEIGHT - 1) return;

  int16_t yOffset = abs(sy) % 8;
  int16_t sRow = sy / 8;
  if(sy < 0){
    sRow--;
    yOffset = 8 - yOffset;
  }
  int16_t rows = (h + 7) / 8;

  int16_t a = 0;
  int16_t iCol = 0;
  uint8_t byte = 0;
  uint16_t bit = 1;
  while(a < rows){
    uint8_t bl = 1;
    while(!cs.get(1)) bl += 2;
    uint16_t len = cs.get(bl) + 1;    // span length

    for(uint16_t i = 0; i < len; i++){
      if(col) byte |= bit;
      bit <<= 1;

      if(bit == 0x100){
        int16_t bRow = sRow + a;
        if(bRow <= (HEIGHT / 8) - 1 && bRow > -2 && iCol + sx <= WIDTH - 1 && iCol + sx >= 0){
          if(bRow >= 0){
            uint8_t *b = &sBuffer[bRow * WIDTH + sx + iCol];
            if(color) *b |= byte << yOffset;
            else *b &= ~(byte << yOffset);
          }
          if(yOffset && bRow < (HEIGHT / 8) - 1 && bRow > -2){
            uint8_t *b = &sBuffer[(bRow + 1) * WIDTH + sx + iCol];
            if(color) *b |= byte >> (8 - yOffset);
            else *b &= ~(byte >> (8 - yOffset));
          }
        }
        iCol++;
        if(iCol >= w){
          iCol = 0;
          a++;
        }
        byte = 0;
        bit = 1;
      }
    }
    col = 1 - col;    // toggle colour for the next span
  }
}

/********************************** text *************************************/

void Arduboy::setCursor(int16_t x, int16_t y){
  cursorX = x;
  cursorY = y;
}

void Arduboy::setTextWrap(bool wrap){
  textWrap = wrap;
}

void Arduboy::setTextSize(uint8_t size){
  textSize = size ? size : 1;
}

// size: every pixel of the font is size x size
void Arduboy::drawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg, uint8_t size){
  const uint8_t *glyph = display_gfx_font_glyph(c);
  for(uint8_t i = 0; i < DISPLAY_GFX_CELL_WIDTH; i++){
    uint8_t column = i < DISPLAY_GFX_FONT_WIDTH ? pgm_read_byte(glyph + i) : 0;
    for(uint8_t j = 0; j < DISPLAY_GFX_CELL_HEIGHT; j++, column >>= 1){
      uint8_t pixel_color = column & 1 ? color : bg;
      if(!(column & 1) && bg == color) continue;
      if(size == 1){
        drawPixel(x + i, y + j, pixel_color);
      }else{
        fillRect(x + i * size, y + j * size, size, size, pixel_color);
      }
    }
  }
}

size_t Arduboy::write(uint8_t c){
  uint8_t cell_width = DISPLAY_GFX_CELL_WIDTH * textSize;
  uint8_t cell_height = DISPLAY_GFX_CELL_HEIGHT * textSize;
  if(c == '\n'){
    cursorX = 0;
    cursorY += cell_height;
  }else if(c != '\r'){
    drawChar(cursorX, cursorY, c, WHITE, BLACK, textSize);
    cursorX += cell_width;
    if(textWrap && cursorX > WIDTH - cell_width){
      cursorX = 0;
      cursorY += cell_height;
    }
  }
  return 1;
}

size_t Arduboy::print(char c){
  return write(c);
}

size_t Arduboy::print(const char *str){
  size_t n = 0;
  while(*str) n += write(*str++);
  return n;
}

size_t Arduboy::print(const __FlashStringHelper *str){
  const char *p = reinterpret_cast<const char *>(str);
  size_t n = 0;
  char c;
  while((c = pgm_read_byte(p++))) n += write(c);
  return n;
}

size_t Arduboy::printNumber(unsigned long n){
  char digits[10];
  uint8_t len = 0;
  do{
    digits[len++] = '0' + n % 10;
    n /= 10;
  }while(n);
  for(uint8_t i = len; i > 0; i--) write(digits[i - 1]);
  return len;
}

// as Arduino Print: unsigned char is printed as a number
size_t Arduboy::print(unsigned char n){
  return printNumber(n);
}

size_t Arduboy::print(int n){
  return print((long)n);
}

size_t Arduboy::print(unsigned int n){
  return printNumber(n);
}

size_t Arduboy::print(long n){
  if(n < 0){
    write('-');
    return printNumber(-n) + 1;
  }
  return printNumber(n);
}

size_t Arduboy::print(unsigned long n){
  return printNumber(n);
}
