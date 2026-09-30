#include <stdlib.h>
#include <util/delay.h>
#include "display.h"
#include "keys.h"
#include "led.h"
#include "vibro.h"
#include "mic.h"
#include "speaker.h"
#include "radio.h"
#include "sd.h"

// keys polling interval, also debounce time
#define POLL_INTERVAL_MS 20
#define LED_BLINK_INTERVAL_MS 500
// mic level that fills the whole bar, less than MIC_LEVEL_MAX
// because loud sound rarely reaches full ADC range
#define MIC_BAR_FULL_LEVEL 512
#define MIC_BAR_CHAR '#'
// speaker test tones, Hz
#define SPEAKER_TONES {250, 500, 1000, 2000, 4000}

static void test_button(void);
static void test_led(void);
static void test_vibro(void);
static void test_mic(void);
static void test_speaker(void);
static void test_radio(void);
static void test_sd(void);

typedef struct {
  const char *name;
  void (*run)(void);
} menu_item_t;

static const menu_item_t menu[] = {
  {"test button", test_button},
  {"test LED",    test_led},
  {"test vibro",  test_vibro},
  {"test mic",    test_mic},
  {"test speaker", test_speaker},
  {"test NRF24L01", test_radio},
  {"test SD card", test_sd},
};
#define MENU_SIZE (sizeof(menu) / sizeof(menu[0]))

static uint8_t selected = 0;

static void draw_menu(void){
  char line[DISPLAY_COLS + 1];
  // scroll the list if it is longer than the screen
  uint8_t first = selected < DISPLAY_ROWS ? 0 : selected - DISPLAY_ROWS + 1;

  for(uint8_t row = 0; row < DISPLAY_ROWS; row++){
    uint8_t item = first + row;
    if(item >= MENU_SIZE){
      display_print_line(row, "");
      continue;
    }
    line[0] = item == selected ? '>' : ' ';
    uint8_t i = 1;
    for(const char *name = menu[item].name; *name && i < DISPLAY_COLS; i++){
      line[i] = *name++;
    }
    line[i] = '\0';
    display_print_line(row, line);
  }
}

// wait for button C to return to the main menu
static void wait_back(void){
  while(keys_get_press() != C_KEY_PRESSED){
    _delay_ms(POLL_INTERVAL_MS);
  }
}

static void test_button(void){
  static const char *const key_names[] = {
    [A_KEY_PRESSED]     = "A",
    [B_KEY_PRESSED]     = "B",
    [C_KEY_PRESSED]     = "C",
    [UP_KEY_PRESSED]    = "UP",
    [DOWN_KEY_PRESSED]  = "DOWN",
    [LEFT_KEY_PRESSED]  = "LEFT",
    [RIGHT_KEY_PRESSED] = "RIGHT",
  };

  display_clear();
  display_print_line(0, "press any button");

  // name is shown while the key is held, redraw only on change
  uint8_t shown_key = NOOP;
  while(1){
    uint8_t key = keys_read();
    if(key == C_KEY_PRESSED){
      // wait for release, otherwise the main menu takes this C as a new press
      while(keys_read() != NOOP){
        _delay_ms(POLL_INTERVAL_MS);
      }
      return;
    }
    if(key != shown_key){
      display_print_line(1, key == NOOP ? "" : key_names[key]);
      shown_key = key;
    }
    _delay_ms(POLL_INTERVAL_MS);
  }
}

static void test_led(void){
  uint16_t elapsed_ms = 0;

  display_clear();
  display_print_line(0, "LED blinking");
  display_print_line(1, "C - back");

  while(keys_get_press() != C_KEY_PRESSED){
    if(elapsed_ms >= LED_BLINK_INTERVAL_MS){
      TOGGLE(LED_PORT, LED_PIN);
      elapsed_ms = 0;
    }
    _delay_ms(POLL_INTERVAL_MS);
    elapsed_ms += POLL_INTERVAL_MS;
  }
  SET_LOW(LED_PORT, LED_PIN);
}

static void test_vibro(void){
  display_clear();
  display_print_line(0, "vibro on");
  display_print_line(1, "C - back");

  SET_HIGH(VIBRO_PORT, VIBRO_PIN);
  wait_back();
  SET_LOW(VIBRO_PORT, VIBRO_PIN);
}

static void test_mic(void){
  char bar[DISPLAY_COLS + 1];
  uint8_t shown_len = 0xFF;

  display_clear();
  display_print_line(1, "C - back");

  while(keys_get_press() != C_KEY_PRESSED){
    uint16_t level = mic_get_level(MIC_LEVEL_SAMPLES);
    if(level > MIC_BAR_FULL_LEVEL) level = MIC_BAR_FULL_LEVEL;
    uint8_t len = (uint32_t)level * DISPLAY_COLS / MIC_BAR_FULL_LEVEL;

    // redraw only on change
    if(len != shown_len){
      for(uint8_t i = 0; i < len; i++){
        bar[i] = MIC_BAR_CHAR;
      }
      bar[len] = '\0';
      display_print_line(0, bar);
      shown_len = len;
    }
    _delay_ms(POLL_INTERVAL_MS);
  }
}

static void draw_tone(uint16_t freq){
  char line[DISPLAY_COLS + 1] = "tone ";
  utoa(freq, line + 5, 10);
  uint8_t len = 5;
  while(line[len]) len++;
  line[len++] = ' ';
  line[len++] = 'H';
  line[len++] = 'z';
  line[len] = '\0';
  display_print_line(0, line);
}

static void test_speaker(void){
  static const uint16_t tones[] = SPEAKER_TONES;
  const uint8_t tones_count = sizeof(tones) / sizeof(tones[0]);
  uint8_t tone = 2;

  display_clear();
  display_print_line(1, "UP/DOWN - tone");
  display_print_line(2, "C - back");
  draw_tone(tones[tone]);
  speaker_tone(tones[tone]);

  while(1){
    switch(keys_get_press()){
      case UP_KEY_PRESSED:
        if(tone < tones_count - 1) tone++;
        break;
      case DOWN_KEY_PRESSED:
        if(tone) tone--;
        break;
      case C_KEY_PRESSED:
        speaker_off();
        return;
      default:
        _delay_ms(POLL_INTERVAL_MS);
        continue;
    }
    draw_tone(tones[tone]);
    speaker_tone(tones[tone]);
    _delay_ms(POLL_INTERVAL_MS);
  }
}

// "NAME  0xHH" register line
static void draw_register(uint8_t row, const char *name, uint8_t value){
  static const char hex[] = "0123456789ABCDEF";
  char line[DISPLAY_COLS + 1];
  uint8_t len = 0;
  while(*name && len < DISPLAY_COLS - 5) line[len++] = *name++;
  line[len++] = ' ';
  line[len++] = '0';
  line[len++] = 'x';
  line[len++] = hex[value >> 4];
  line[len++] = hex[value & 0x0F];
  line[len] = '\0';
  display_print_line(row, line);
}

static void test_radio(void){
  display_clear();
  if(radio_is_present()){
    display_print_line(0, "NRF24L01 OK");
  }else{
    display_print_line(0, "NRF24L01 error");
  }
  draw_register(1, "STATUS", radio_read_register(RADIO_STATUS));
  draw_register(2, "CONFIG", radio_read_register(RADIO_CONFIG));
  display_print_line(3, "C - back");
  wait_back();
}

static void test_sd(void){
  static uint8_t sector[SD_SECTOR_SIZE];
  static const char *const type_names[] = {
    [SD_TYPE_NONE] = "",
    [SD_TYPE_V1]   = "type SD v1",
    [SD_TYPE_V2]   = "type SD v2",
    [SD_TYPE_SDHC] = "type SDHC",
  };

  display_clear();
  display_print_line(0, "SD init...");

  uint8_t type = sd_init_card();
  uint8_t read_ok = type != SD_TYPE_NONE && sd_read_sector(0, sector);

  // SD traffic corrupts display RAM, so draw the whole screen after it
  display_clear();
  display_print_line(3, "C - back");
  if(type == SD_TYPE_NONE){
    display_print_line(0, "SD init error");
  }else{
    display_print_line(1, type_names[type]);
    if(!read_ok){
      display_print_line(0, "SD read error");
    }else if(sector[SD_MBR_SIGNATURE] != 0x55 || sector[SD_MBR_SIGNATURE + 1] != 0xAA){
      display_print_line(0, "SD no MBR");
    }else{
      display_print_line(0, "SD OK");
      draw_register(2, "part type", sector[SD_MBR_PARTITION_TYPE]);
    }
  }
  wait_back();
}

int main(void){
  init_keys();
  init_led();
  init_vibro();
  init_speaker();
  // CSN high before any SPI traffic, otherwise radio catches display data
  init_radio();
  init_sd();
  init_display();
  init_mic();
  draw_menu();

  while(1){
    switch(keys_get_press()){
      case UP_KEY_PRESSED:
        selected = selected ? selected - 1 : MENU_SIZE - 1;
        draw_menu();
        break;
      case DOWN_KEY_PRESSED:
        selected = selected < MENU_SIZE - 1 ? selected + 1 : 0;
        draw_menu();
        break;
      case A_KEY_PRESSED:
        menu[selected].run();
        display_clear();
        draw_menu();
        break;
      case C_KEY_PRESSED:
        menu_exit_app();
        draw_menu();
        break;
    }
    _delay_ms(POLL_INTERVAL_MS);
  }
}
