#include <string.h>
#include <stdlib.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <util/atomic.h>
#include "display.h"
#include "display_gfx.h"
#include "keys.h"
#include "radio.h"
#include "led.h"
#include "speaker.h"
#include "mic.h"
#include "loader.h"
#include "ui.h"

// Morse code: the text of the input field (bottom of the screen) goes by the
// speaker or the LED in Russian or English Morse code, the code from the
// microphone is decoded to the top of the screen in the selected language.
//
// input: UP / DOWN - the char at the cursor, LEFT / RIGHT - the cursor,
//        A - send, B - delete the char, C - menu
//
// Timer1 takes the microphone 8000 times per second, 8 Goertzel filters
// (500..1200 Hz) find a tone in blocks of 10 ms: one frequency must be much
// stronger than the others (the microphone has AGC, so the level can not be
// used). The interrupt collects lengths of tone / silence into a queue, the
// main loop decodes them, so slow drawing of the screen does not lose them.

#define POLL_INTERVAL_MS    20

/********************************* screen ************************************/

#define COLS                DISPLAY_GFX_COLS
#define STATUS_ROW          0
#define RX_ROW              1
#define RX_ROWS             5
#define INPUT_ROW           (RX_ROW + RX_ROWS)
#define INPUT_ROWS          (DISPLAY_GFX_ROWS - INPUT_ROW)
#define RX_SIZE             (RX_ROWS * COLS)
#define INPUT_SIZE          (INPUT_ROWS * COLS)

// SPI clock fosc/16 = 1 MHz for the display (the libraries leave fosc/64)
#define SPI_1MHZ()          do{ \
    SPCR = (SPCR & ~(_BV(SPR1)|_BV(SPR0))) | _BV(SPR0); \
    SPSR &= ~_BV(SPI2X); \
  }while(0)

/********************************* morse *************************************/

// glyph of the display font and the code
typedef struct {
  uint8_t glyph;
  uint8_t code;
} morse_t;

#define CYR(n)              (DISPLAY_GFX_CYRILLIC + (n))
#define CODE_START          1       // code without elements
#define CODE_INVALID        0

// generated: code = 1 (start bit), then elements from the first one: 0 - dot, 1 - dash
static const morse_t morse_en[] PROGMEM = {
  {'A', 0x05}, {'B', 0x18}, {'C', 0x1A}, {'D', 0x0C}, {'E', 0x02},
  {'F', 0x12}, {'G', 0x0E}, {'H', 0x10}, {'I', 0x04}, {'J', 0x17},
  {'K', 0x0D}, {'L', 0x14}, {'M', 0x07}, {'N', 0x06}, {'O', 0x0F},
  {'P', 0x16}, {'Q', 0x1D}, {'R', 0x0A}, {'S', 0x08}, {'T', 0x03},
  {'U', 0x09}, {'V', 0x11}, {'W', 0x0B}, {'X', 0x19}, {'Y', 0x1B},
  {'Z', 0x1C}, {'0', 0x3F}, {'1', 0x2F}, {'2', 0x27}, {'3', 0x23},
  {'4', 0x21}, {'5', 0x20}, {'6', 0x30}, {'7', 0x38}, {'8', 0x3C},
  {'9', 0x3E}, {'.', 0x55}, {',', 0x73}, {'?', 0x4C}, {'\'', 0x5E},
  {'!', 0x6B}, {'/', 0x32}, {'(', 0x36}, {')', 0x6D}, {':', 0x78},
  {'=', 0x31}, {'+', 0x2A}, {'-', 0x61}, {'"', 0x52}, {'@', 0x5A},
};

static const morse_t morse_ru[] PROGMEM = {
  {CYR(0), 0x05}, {CYR(1), 0x18}, {CYR(2), 0x0B}, {CYR(3), 0x0E},
  {CYR(4), 0x0C}, {CYR(5), 0x02}, {CYR(6), 0x11}, {CYR(7), 0x1C},
  {CYR(8), 0x04}, {CYR(9), 0x17}, {CYR(10), 0x0D}, {CYR(11), 0x14},
  {CYR(12), 0x07}, {CYR(13), 0x06}, {CYR(14), 0x0F}, {CYR(15), 0x16},
  {CYR(16), 0x0A}, {CYR(17), 0x08}, {CYR(18), 0x03}, {CYR(19), 0x09},
  {CYR(20), 0x12}, {CYR(21), 0x10}, {CYR(22), 0x1A}, {CYR(23), 0x1E},
  {CYR(24), 0x1F}, {CYR(25), 0x1D}, {CYR(26), 0x3B}, {CYR(27), 0x1B},
  {CYR(28), 0x19}, {CYR(29), 0x24}, {CYR(30), 0x13}, {CYR(31), 0x15},
  {'0', 0x3F}, {'1', 0x2F}, {'2', 0x27}, {'3', 0x23}, {'4', 0x21},
  {'5', 0x20}, {'6', 0x30}, {'7', 0x38}, {'8', 0x3C}, {'9', 0x3E},
  {'.', 0x40}, {',', 0x55}, {'?', 0x4C}, {'!', 0x73}, {'-', 0x61},
  {'/', 0x32}, {':', 0x78}, {';', 0x6A}, {'"', 0x52},
};

#define LANG_RU             0
#define LANG_EN             1

#define OUTPUT_SPEAKER      0
#define OUTPUT_LED          1

#define TONE_FREQ           700
#define WPM_MIN             5
#define WPM_MAX             40
#define WPM_DEFAULT         12
// PARIS: a dot is 1200 / WPM ms
#define DOT_MS(wpm)         (1200 / (wpm))
// decoder: the dot length follows the sender
#define DOT_MS_MIN          20
#define DOT_MS_MAX          400
// the own signal (echo) is not decoded for a while after sending
#define ECHO_MS             200

static uint8_t lang = LANG_RU;
static uint8_t output = OUTPUT_SPEAKER;
static uint8_t wpm = WPM_DEFAULT;

static const morse_t *table(uint8_t language, uint8_t *count){
  if(language == LANG_RU){
    *count = sizeof(morse_ru) / sizeof(morse_ru[0]);
    return morse_ru;
  }
  *count = sizeof(morse_en) / sizeof(morse_en[0]);
  return morse_en;
}

static uint8_t is_cyrillic(uint8_t glyph){
  return glyph >= DISPLAY_GFX_CYRILLIC && glyph <= DISPLAY_GFX_LAST;
}

static uint8_t is_latin(uint8_t glyph){
  return glyph >= 'A' && glyph <= 'Z';
}

// code of the glyph: letters by their alphabet, the rest by the language
static uint8_t encode(uint8_t glyph){
  uint8_t count;
  uint8_t language = is_cyrillic(glyph) ? LANG_RU : is_latin(glyph) ? LANG_EN : lang;
  const morse_t *t = table(language, &count);
  for(uint8_t i = 0; i < count; i++){
    if(pgm_read_byte(&t[i].glyph) == glyph) return pgm_read_byte(&t[i].code);
  }
  return CODE_INVALID;
}

static uint8_t decode(uint8_t code){
  uint8_t count;
  const morse_t *t = table(lang, &count);
  for(uint8_t i = 0; i < count; i++){
    if(pgm_read_byte(&t[i].code) == code) return pgm_read_byte(&t[i].glyph);
  }
  return DISPLAY_GFX_UNKNOWN;
}

/****************************** tone detector ********************************/

#define SAMPLE_RATE         8000UL
#define TICKS_PER_MS        (SAMPLE_RATE / 1000)
#define TIMER1_CLOCK        (F_CPU / 8)
// block 80 samples = 10 ms, bins every 100 Hz: 500..1200 Hz
#define BLOCK               80
#define BLOCK_MS            10
#define BINS                8
// tone: the strongest bin is RATIO_ON (to start) / RATIO_OFF (to stay)
// times stronger than the noise, and RATIO_PEAK times stronger than the
// mean of the other bins of the block (a click is in all bins). The noise is
// the mean of the other bins in silence, it follows the AGC of the
// microphone in 2^NOISE_SHIFT blocks (~0.3 s). The sums of the other
// BINS - 1 bins are used instead of the means (no division in the interrupt). The level of a tone with
// amplitude A at the bin is (BLOCK * A / 2)^2, POWER_MIN is A ~ 5
#define RATIO_ON            10
#define RATIO_OFF           5
#define RATIO_PEAK          3
#define NOISE_SHIFT         5
#define POWER_MIN           40000L
// a change shorter than DEBOUNCE blocks is a glitch
#define DEBOUNCE            2
#define QUEUE_SIZE          16
#define EVENT_MARK          0x8000
#define RUN_MAX             0x7FFF

// 2 * cos(2 pi k / BLOCK) in Q14 for k = 5..12
static const int16_t coeff[BINS] PROGMEM = {
  30274, 29197, 27939, 26510, 24917, 23170, 21281, 19261
};

static int16_t s1[BINS];
static int32_t noise;       // the sum of BINS - 1 bins in silence
static int16_t s2[BINS];
static uint8_t block_pos;

static volatile uint32_t ticks;             // samples
static volatile uint8_t listening;
static volatile uint8_t tone;               // debounced state
static volatile uint16_t run;               // blocks of the state
static uint8_t pending;                     // blocks of the other state
// lengths (in blocks) of tone (EVENT_MARK) and silence
static volatile uint16_t queue[QUEUE_SIZE];
static volatile uint8_t queue_head;
static volatile uint8_t queue_tail;

static void push(uint16_t event){
  uint8_t next = (queue_head + 1) % QUEUE_SIZE;
  if(next == queue_tail) return;    // the main loop is too slow: lost
  queue[queue_head] = event;
  queue_head = next;
}

static void block_end(void){
  int32_t max = 0;
  int32_t sum = 0;
  for(uint8_t k = 0; k < BINS; k++){
    int16_t c = pgm_read_word(&coeff[k]);
    // |c * s1 >> 14| < 2 |s1| fits int16: only 16 x 16 bit multiplies
    // (the interrupt has 2000 clocks per sample)
    int16_t cs = ((int32_t)c * s1[k]) >> 14;
    int32_t p = (int32_t)s1[k] * s1[k] + (int32_t)s2[k] * s2[k]
              - (int32_t)cs * s2[k];
    if(p > max) max = p;
    sum += p;
    s1[k] = s2[k] = 0;
  }
  if(!listening){
    tone = 0;
    run = 0;
    pending = 0;
    return;
  }

  int32_t others = sum - max;
  if(!noise){
    noise = others;
  }else if(!tone){
    noise += (others - noise) >> NOISE_SHIFT;
  }
  // max > mean * ratio, mean = others / (BINS - 1)
  int32_t max_n = max * (BINS - 1);
  uint8_t raw = max > POWER_MIN && max_n > others * RATIO_PEAK
             && max_n > noise * (tone ? RATIO_OFF : RATIO_ON);

  if(raw == tone){
    run += 1 + pending;
    pending = 0;
  }else if(++pending >= DEBOUNCE){
    push(run | (tone ? EVENT_MARK : 0));
    tone = raw;
    run = pending;
    pending = 0;
  }
  if(run > RUN_MAX) run = RUN_MAX;
}

ISR(TIMER1_COMPA_vect){
  int16_t x = (int16_t)ADCH - 128;
  ADCSRA |= _BV(ADSC);
  ticks++;

  for(uint8_t k = 0; k < BINS; k++){
    int16_t c = pgm_read_word(&coeff[k]);
    int16_t s = x + (int16_t)(((int32_t)c * s1[k]) >> 14) - s2[k];
    s2[k] = s1[k];
    s1[k] = s;
  }
  if(++block_pos == BLOCK){
    block_pos = 0;
    block_end();
  }
}

static void init_detector(void){
  init_mic();
  // 8 bit result, ADC clock fosc/64: conversion 52 us < 125 us of a sample
  ADMUX |= _BV(ADLAR);
  ADCSRA = _BV(ADEN)|_BV(ADPS2)|_BV(ADPS1);
  ADCSRA |= _BV(ADSC);

  TCCR1A = 0;
  TCCR1B = _BV(WGM12)|_BV(CS11);    // CTC, clk/8
  OCR1A = TIMER1_CLOCK / SAMPLE_RATE - 1;
  TIMSK1 = _BV(OCIE1A);
  listening = 1;
  sei();
}

static uint32_t now_ticks(void){
  uint32_t t;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
    t = ticks;
  }
  return t;
}

/********************************* texts *************************************/

static uint8_t rx[RX_SIZE];
static uint8_t rx_len;
static uint8_t input[INPUT_SIZE];
static uint8_t input_len;
static uint8_t cursor;
static uint8_t last_glyph;          // the new char starts from it
static uint8_t dirty;               // rows to draw, bit per row
static uint8_t sending = 0xFF;      // the char being sent (cursor), 0xFF - no

#define DIRTY_ALL           0xFF

static void mark_row(uint8_t row){
  dirty |= 1 << row;
}

static void mark_rx(void){
  for(uint8_t row = RX_ROW; row < RX_ROW + RX_ROWS; row++) mark_row(row);
}

static void mark_input(void){
  for(uint8_t row = INPUT_ROW; row < INPUT_ROW + INPUT_ROWS; row++) mark_row(row);
}

// received char to the end of the text, the text goes up when it is full
static void rx_add(uint8_t glyph){
  if(rx_len == RX_SIZE){
    memmove(rx, rx + COLS, RX_SIZE - COLS);
    rx_len -= COLS;
    mark_rx();
  }
  rx[rx_len] = glyph;
  mark_row(RX_ROW + rx_len / COLS);
  rx_len++;
}

/******************************** decoder ************************************/

// tone lengths of the current char are classified at the end of the char:
// a char with short and long tones gets the threshold between them, else
// the dot length decides. The dot length follows the sender: the short
// silences in the char are dots, a tone longer than 6 dots is a dash of a
// slower sender
#define CHAR_MARKS          6

static uint16_t marks[CHAR_MARKS + 1];
static uint8_t mark_count;
static uint16_t dot_ms = DOT_MS(WPM_DEFAULT);
static uint8_t word_done = 1;

static void dot_limit(void){
  if(dot_ms < DOT_MS_MIN) dot_ms = DOT_MS_MIN;
  if(dot_ms > DOT_MS_MAX) dot_ms = DOT_MS_MAX;
}

static void decoder_reset(void){
  mark_count = 0;
  word_done = 1;
  dot_ms = DOT_MS(wpm);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
    queue_tail = queue_head;
  }
}

static void char_end(void){
  if(mark_count > CHAR_MARKS){
    rx_add(DISPLAY_GFX_UNKNOWN);
    mark_count = 0;
    return;
  }
  uint16_t min = marks[0], max = marks[0];
  for(uint8_t i = 1; i < mark_count; i++){
    if(marks[i] < min) min = marks[i];
    if(marks[i] > max) max = marks[i];
  }
  uint16_t threshold = max >= 2 * min ? (min + max) / 2 : 2 * dot_ms;
  uint8_t code = CODE_START;
  for(uint8_t i = 0; i < mark_count; i++){
    if(marks[i] < threshold){
      code <<= 1;
      dot_ms = (3 * dot_ms + marks[i]) / 4;
    }else{
      code = code << 1 | 1;
      dot_ms = (3 * dot_ms + marks[i] / 3) / 4;
    }
  }
  dot_limit();
  rx_add(decode(code));
  mark_count = 0;
}

static void mark_end(uint16_t ms){
  if(ms > 6 * dot_ms){
    dot_ms = ms / 3;
    dot_limit();
  }
  if(mark_count <= CHAR_MARKS) marks[mark_count] = ms;
  if(mark_count < CHAR_MARKS + 1) mark_count++;
  word_done = 0;
}

// silence of ms (done: it is over): the end of the char after 3 dots,
// of the word after 7 (half way between them)
static void gap(uint16_t ms, uint8_t done){
  if(mark_count && ms >= 2 * dot_ms){
    char_end();
  }else if(done && mark_count){
    dot_ms = ms < dot_ms ? (dot_ms + ms) / 2 : (3 * dot_ms + ms) / 4;
    dot_limit();
  }
  if(!mark_count && !word_done && ms >= 5 * dot_ms){
    rx_add(' ');
    word_done = 1;
  }
}

static void decoder_poll(void){
  while(queue_tail != queue_head){
    uint16_t event = queue[queue_tail];
    queue_tail = (queue_tail + 1) % QUEUE_SIZE;
    uint16_t ms = (event & RUN_MAX) * BLOCK_MS;
    if(event & EVENT_MARK){
      mark_end(ms);
    }else{
      gap(ms, 1);
    }
  }
  // the silence now: the last char without waiting for the next tone
  uint8_t on;
  uint16_t blocks;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
    on = tone;
    blocks = run;
  }
  if(!on) gap(blocks * BLOCK_MS, 0);
}

/********************************* sender ************************************/

static void signal(uint8_t on){
  if(output == OUTPUT_SPEAKER){
    if(on){
      speaker_tone(TONE_FREQ);
    }else{
      speaker_off();
    }
  }else if(on){
    SET_HIGH(LED_PORT, LED_PIN);
  }else{
    SET_LOW(LED_PORT, LED_PIN);
  }
}

// returns 0 if C is pressed (cancel)
static uint8_t wait_ms(uint16_t ms){
  uint32_t end = now_ticks() + (uint32_t)ms * TICKS_PER_MS;
  while((int32_t)(now_ticks() - end) < 0){
    if(keys_read() == C_KEY_PRESSED) return 0;
  }
  return 1;
}

static void draw(void);

static void send(void){
  uint16_t dot = DOT_MS(wpm);
  uint8_t ok = 1;

  listening = 0;
  ui_wait_release();
  for(uint8_t i = 0; i < input_len && ok; i++){
    sending = i;
    mark_input();
    mark_row(STATUS_ROW);
    draw();

    uint8_t glyph = input[i];
    if(glyph == ' '){
      // 7 dots between words, 3 of them are after the last char
      ok = wait_ms(4 * dot);
      continue;
    }
    uint8_t c = encode(glyph);
    if(c == CODE_INVALID) continue;
    // elements from the bit after the start bit
    uint8_t bit = 0x80;
    while(!(c & bit)) bit >>= 1;
    for(bit >>= 1; bit && ok; bit >>= 1){
      signal(1);
      ok = wait_ms(c & bit ? 3 * dot : dot);
      signal(0);
      if(ok) ok = wait_ms(dot);
    }
    // 3 dots between chars, 1 is after the last element
    if(ok) ok = wait_ms(2 * dot);
  }
  signal(0);
  sending = 0xFF;
  mark_input();
  mark_row(STATUS_ROW);
  wait_ms(ECHO_MS);
  ui_wait_release();
  decoder_reset();
  listening = 1;
}

/******************************** drawing ************************************/

static uint8_t shown_tone;

static void draw_status(void){
  char line[COLS + 1];
  memset(line, ' ', COLS);
  line[COLS] = '\0';
  // "RU SPK  TX12  RX12  *"
  memcpy_P(line, lang == LANG_RU ? PSTR("RU") : PSTR("EN"), 2);
  memcpy_P(line + 3, output == OUTPUT_SPEAKER ? PSTR("SPK") : PSTR("LED"), 3);
  memcpy_P(line + 8, PSTR("TX"), 2);
  utoa(wpm, line + 10, 10);
  memcpy_P(line + 14, PSTR("RX"), 2);
  utoa(1200 / dot_ms, line + 16, 10);
  for(uint8_t i = 0; i < COLS; i++){
    if(!line[i]) line[i] = ' ';
  }
  line[COLS - 1] = sending != 0xFF ? '>' : shown_tone ? '*' : ' ';
  display_gfx_row(STATUS_ROW, line, COLS, DISPLAY_GFX_NO_CURSOR, 1);
}

static void draw(void){
  if(tone != shown_tone){
    shown_tone = tone;
    mark_row(STATUS_ROW);
  }
  for(uint8_t row = 0; row < DISPLAY_GFX_ROWS; row++){
    if(!(dirty & (1 << row))) continue;
    if(row == STATUS_ROW){
      draw_status();
    }else if(row < INPUT_ROW){
      uint8_t from = (row - RX_ROW) * COLS;
      uint8_t len = rx_len > from ? rx_len - from : 0;
      display_gfx_row(row, (char *)rx + from, len < COLS ? len : COLS, DISPLAY_GFX_NO_CURSOR, 0);
    }else{
      uint8_t from = (row - INPUT_ROW) * COLS;
      uint8_t len = input_len > from ? input_len - from : 0;
      uint8_t mark = sending != 0xFF ? sending : cursor;
      uint8_t col = mark >= from && mark < from + COLS ? mark - from : DISPLAY_GFX_NO_CURSOR;
      display_gfx_row(row, (char *)input + from, len < COLS ? len : COLS, col, 0);
    }
  }
  dirty = 0;
}

/********************************* input *************************************/

// chars of the language: the space, then the code table
static uint8_t charset_glyph(uint8_t index){
  uint8_t count;
  const morse_t *t = table(lang, &count);
  return index ? pgm_read_byte(&t[index - 1].glyph) : ' ';
}

static uint8_t charset_size(void){
  uint8_t count;
  table(lang, &count);
  return count + 1;
}

static uint8_t charset_index(uint8_t glyph){
  uint8_t size = charset_size();
  for(uint8_t i = 0; i < size; i++){
    if(charset_glyph(i) == glyph) return i;
  }
  return 0;
}

static void first_letter(void){
  last_glyph = charset_glyph(1);
}

// UP / DOWN: the char at the cursor goes along the chars of the language,
// at the end a new char is added (the last chosen one)
static void change_char(int8_t step){
  if(cursor == input_len){
    if(input_len == INPUT_SIZE) return;
    input[input_len++] = last_glyph;
  }else{
    uint8_t size = charset_size();
    uint8_t index = charset_index(input[cursor]);
    input[cursor] = charset_glyph((index + size + step) % size);
  }
  last_glyph = input[cursor];
  mark_input();
}

static void delete_char(void){
  if(cursor == input_len){
    if(!cursor) return;
    cursor--;
  }
  memmove(input + cursor, input + cursor + 1, input_len - cursor - 1);
  input_len--;
  mark_input();
}

/********************************** menu *************************************/

#define MENU_LANG       0
#define MENU_OUTPUT     1
#define MENU_SPEED      2
#define MENU_CLEAR_RX   3
#define MENU_CLEAR_IN   4
#define MENU_EXIT       5
#define MENU_ITEMS      6

// UP / DOWN - WPM, A - ok, C - cancel
static void speed_edit(void){
  uint8_t value = wpm;
  display_print_line_P(0, PSTR("speed, WPM"));
  display_print_line_P(2, PSTR(""));
  display_print_line_P(3, PSTR("A-ok C-cancel"));
  while(1){
    char line[DISPLAY_COLS + 1];
    strcpy_P(line, PSTR("  "));
    utoa(value, line + 2, 10);
    display_print_line(1, line);
    switch(ui_wait_key()){
      case UP_KEY_PRESSED:
        if(value < WPM_MAX) value++;
        break;
      case DOWN_KEY_PRESSED:
        if(value > WPM_MIN) value--;
        break;
      case A_KEY_PRESSED:
        wpm = value;
        decoder_reset();
        return;
      case C_KEY_PRESSED:
        return;
    }
  }
}

static void run_menu(void){
  static const char items[MENU_ITEMS][16] PROGMEM = {
    "language RU/EN", "speaker / LED", "speed", "clear received", "clear input", "exit"
  };

  listening = 0;
  ui_wait_release();
  display_gfx_off();
  switch(ui_menu((const char *)items, sizeof(items[0]), MENU_ITEMS)){
    case MENU_LANG:
      lang = lang == LANG_RU ? LANG_EN : LANG_RU;
      first_letter();
      break;
    case MENU_OUTPUT:
      output = output == OUTPUT_SPEAKER ? OUTPUT_LED : OUTPUT_SPEAKER;
      break;
    case MENU_SPEED:
      speed_edit();
      break;
    case MENU_CLEAR_RX:
      rx_len = 0;
      break;
    case MENU_CLEAR_IN:
      input_len = 0;
      cursor = 0;
      break;
    case MENU_EXIT:
      if(!ui_exit_confirm()) break;
      if(!loader_is_present()){
        ui_message(PSTR("no bootloader"), NULL);
        break;
      }
      cli();
      loader_load_default_app();
  }
  ui_wait_release();
  display_gfx_on();
  dirty = DIRTY_ALL;
  decoder_reset();
  listening = 1;
}

/********************************** main *************************************/

int main(void){
  init_keys();
  init_led();
  init_speaker();
  // CSN high before any SPI traffic, otherwise radio catches display data
  init_radio();
  init_display();
  SPI_1MHZ();
  display_gfx_on();

  first_letter();
  init_detector();
  decoder_reset();
  dirty = DIRTY_ALL;

  while(1){
    decoder_poll();
    draw();

    uint8_t key = keys_get_repeat(POLL_INTERVAL_MS);
    switch(key){
      case UP_KEY_PRESSED:
        change_char(1);
        break;
      case DOWN_KEY_PRESSED:
        change_char(-1);
        break;
      case LEFT_KEY_PRESSED:
        if(cursor) cursor--;
        mark_input();
        break;
      case RIGHT_KEY_PRESSED:
        if(cursor < input_len && cursor < INPUT_SIZE - 1) cursor++;
        mark_input();
        break;
      case A_KEY_PRESSED:
        send();
        break;
      case B_KEY_PRESSED:
        delete_char();
        break;
      case C_KEY_PRESSED:
        run_menu();
        break;
    }
    _delay_ms(POLL_INTERVAL_MS);
  }
}
