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

#define EXIT_POLL_INTERVAL_MS   20

static volatile unsigned long timer_millis;

// tone player: one tone or a sequence of (frequency, duration) pairs
static volatile unsigned long tone_end;
static volatile bool tone_timed;
static const uint16_t * volatile tone_seq;
static const uint16_t *tone_seq_start;
static bool tone_seq_in_ram;

// GDRAM line checksums: the line is sent only if it is changed
static uint16_t line_hash[LCD_GDRAM_LINES];
static bool hash_valid;

static uint16_t tone_seq_read(void){
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
    tone_seq_start = tones;
    tone_seq = tones;
    tone_seq_in_ram = in_ram;
    tone_seq_next();
  }
}

void arduboy_no_tone(void){
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
    tone_seq = 0;
    tone_timed = false;
  }
  speaker_off();
}

bool arduboy_tone_playing(void){
  return TCCR0B != 0;
}

void ArduboyTunes::tone(unsigned int freq, unsigned long duration){
  arduboy_tone(freq, duration);
}

void ArduboyTunes::noTone(void){
  arduboy_no_tone();
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
  hash_valid = false;
  init_timer();

  setFrameRate(60);
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

void Arduboy::setFrameRate(uint8_t rate){
  eachFrameMillis = 1000 / rate;
}

bool Arduboy::nextFrame(void){
  static bool c_was_pressed;
  bool c_pressed = CHECK_PIN(BUTTON_C_PINS, BUTTON_C_PIN);
  if(c_pressed && !c_was_pressed) exitMenu();
  c_was_pressed = c_pressed;

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
  srand((unsigned int)(seed ^ (seed >> 16) ^ TCNT2));
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
// any other key returns to the game
void Arduboy::exitMenu(void){
  uint8_t key;
  const uint8_t box_x = 14, box_y = 18, box_w = WIDTH - 2 * box_x, box_h = 28;

  tunes.noTone();
  fillRect(box_x, box_y, box_w, box_h, BLACK);
  drawRect(box_x, box_y, box_w, box_h, WHITE);
  setCursor((WIDTH - 9 * 6) / 2, box_y + 6);
  print("EXIT APP?");
  setCursor((WIDTH - 7 * 6) / 2, box_y + 15);
  print("C - YES");
  display();

  while(keys_read() == C_KEY_PRESSED) _delay_ms(EXIT_POLL_INTERVAL_MS);
  while((key = keys_read()) == NOOP) _delay_ms(EXIT_POLL_INTERVAL_MS);

  if(key == C_KEY_PRESSED && loader_is_present()){
    clear();
    display();
    // back to the text mode and the speed of the libraries
    display_send_command(LCD_EXTENDED_FUNCTION);
    display_send_command(LCD_BASIC_FUNCTION);
    init_spi();
    display_clear();
    display_print_line_P(1, PSTR("   loading..."));
    loader_load_default_app();
  }

  // the key must not get into the game
  while(keys_read() != NOOP) _delay_ms(EXIT_POLL_INTERVAL_MS);
  nextFrameStart = millis();
}

/********************************** screen ***********************************/

uint8_t Arduboy::sBuffer[ARDUBOY_BUFFER_SIZE];

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
  uint8_t line[2 * LCD_LINE_BYTES];
  uint8_t invert_mask = inverted ? 0xFF : 0x00;

  for(uint8_t y = 0; y < LCD_GDRAM_LINES; y++){
    convert_row(image, y, line, invert_mask);
    convert_row(image, y + LCD_GDRAM_LINES, line + LCD_LINE_BYTES, invert_mask);

    // Fletcher-16
    uint8_t sum1 = 0, sum2 = 0;
    for(uint8_t i = 0; i < sizeof(line); i++){
      sum1 += line[i];
      sum2 += sum1;
    }
    uint16_t hash = (sum2 << 8) | sum1;
    if(hash_valid && line_hash[y] == hash) continue;
    line_hash[y] = hash;

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
  hash_valid = true;
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

/********************************** text *************************************/

void Arduboy::setCursor(int16_t x, int16_t y){
  cursorX = x;
  cursorY = y;
}

void Arduboy::setTextWrap(bool wrap){
  textWrap = wrap;
}

void Arduboy::drawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg){
  const uint8_t *glyph = display_gfx_font_glyph(c);
  for(uint8_t i = 0; i < DISPLAY_GFX_CELL_WIDTH; i++){
    uint8_t column = i < DISPLAY_GFX_FONT_WIDTH ? pgm_read_byte(glyph + i) : 0;
    for(uint8_t j = 0; j < DISPLAY_GFX_CELL_HEIGHT; j++, column >>= 1){
      if(column & 1){
        drawPixel(x + i, y + j, color);
      }else if(bg != color){
        drawPixel(x + i, y + j, bg);
      }
    }
  }
}

size_t Arduboy::write(uint8_t c){
  if(c == '\n'){
    cursorX = 0;
    cursorY += DISPLAY_GFX_CELL_HEIGHT;
  }else if(c != '\r'){
    drawChar(cursorX, cursorY, c, WHITE, BLACK);
    cursorX += DISPLAY_GFX_CELL_WIDTH;
    if(textWrap && cursorX > WIDTH - DISPLAY_GFX_CELL_WIDTH){
      cursorX = 0;
      cursorY += DISPLAY_GFX_CELL_HEIGHT;
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
