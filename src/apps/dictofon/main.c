#include <string.h>
#include <stdlib.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <util/atomic.h>
#include "display.h"
#include "keys.h"
#include "radio.h"
#include "mic.h"
#include "sd.h"
#include "fat16_dir.h"
#include "fat16_edit.h"
#include "loader.h"
#include "ui.h"

// Dictaphone: voice from the microphone to /AUDIO/A0000001.WAV, A0000002.WAV ...
// (the number after the biggest one in the directory), WAV 8 bit mono 8 kHz.
//
// list:   UP / DOWN - record, A - play, B - new recording, C - menu
// record: A / B - stop and save, C - cancel
// play:   A - pause, LEFT / RIGHT - back / forward, UP / DOWN - previous /
//         next record, B - volume, C - back to the list
//
// Timer1 (sample rate) takes the microphone ADC or gives samples to Timer0
// PWM on the speaker pin (OC0B). Samples go through 2 sector buffers: the
// interrupt fills / plays one, the main loop writes / reads the other.

#define POLL_INTERVAL_MS    20
#define SHOW_INTERVAL_MS    200

#define SAMPLE_RATE         8000UL
#define WAV_HEADER_SIZE     44
#define SILENCE             128
// Timer1 clk/8: 2 MHz
#define TIMER1_CLOCK        (F_CPU / 8)
#define SAMPLE_RATE_MIN     1000
#define SAMPLE_RATE_MAX     22050

// the microphone bias is removed: dc has DC_FRACTION bits of fraction
// (it fits int16 math), it follows the bias in 2^DC_SHIFT samples (~16 ms)
#define DC_FRACTION         6
#define DC_SHIFT            7

#define SEEK_SECONDS        5
#define LEVEL_COLS          DISPLAY_COLS

// SPI clock after the card init: fosc/16 = 1 MHz is good for the card and
// for the display (the libraries leave fosc/64)
#define SPI_1MHZ()          do{ \
    SPCR = (SPCR & ~(_BV(SPR1)|_BV(SPR0))) | _BV(SPR0); \
    SPSR &= ~_BV(SPI2X); \
  }while(0)

#define LIST_ROWS           (DISPLAY_ROWS - 1)
#define NUMBER_DIGITS       7
#define NUMBER_MAX          9999999UL

#define MENU_DELETE         0
#define MENU_EXIT           1
#define MENU_ITEMS          2

static const uint8_t audio_dir_name[FAT16_RAW_NAME_SIZE] PROGMEM = "AUDIO      ";

// work buffer of FAT16 functions
static uint8_t sector[SD_SECTOR_SIZE];
// samples
static uint8_t samples[2][SD_SECTOR_SIZE];

static uint16_t audio_dir;

/******************************* interrupt ***********************************/

#define MODE_OFF        0
#define MODE_RECORD     1
#define MODE_PLAY       2

static volatile uint8_t mode;
static volatile uint8_t current;            // buffer of the interrupt
static volatile uint16_t position;          // in the current buffer
static volatile uint16_t length[2];         // play: bytes in the buffer
// record: the buffer is full; play: the buffer is played, it can be filled
static volatile uint8_t done[2];
static volatile uint8_t overrun;            // record: samples are lost
static volatile uint8_t peak;               // record: level since the last show

// play: PWM duty is sample * volume / VOLUME_MAX, the bias goes down with
// the signal, so the current through the speaker is lower too
#define VOLUME_MAX      4
#define VOLUME_SHIFT    2
static volatile uint8_t volume = VOLUME_MAX;

static uint8_t volume_level(uint8_t sample){
  return ((uint16_t)sample * volume) >> VOLUME_SHIFT;
}
static uint16_t dc;

ISR(TIMER1_COMPA_vect){
  if(mode == MODE_RECORD){
    uint8_t adc = ADCH;
    // the next conversion is ready before the next interrupt
    ADCSRA |= _BV(ADSC);

    dc += (int16_t)(((int16_t)adc << DC_FRACTION) - (int16_t)dc) >> DC_SHIFT;
    int16_t value = (int16_t)adc - (dc >> DC_FRACTION);
    uint8_t level = value < 0 ? -value : value;
    if(level > peak) peak = level;
    value += SILENCE;
    if(value < 0) value = 0;
    if(value > 255) value = 255;

    if(position == SD_SECTOR_SIZE){
      // the main loop did not write the other buffer yet
      if(done[current ^ 1]){
        overrun = 1;
        return;
      }
      current ^= 1;
      position = 0;
    }
    samples[current][position++] = value;
    if(position == SD_SECTOR_SIZE) done[current] = 1;
  }else if(mode == MODE_PLAY){
    if(position >= length[current]){
      // the other buffer is not read yet: silence
      if(done[current ^ 1]){
        OCR0B = volume_level(SILENCE);
        return;
      }
      done[current] = 1;
      current ^= 1;
      position = 0;
      if(!length[current]) return;
    }
    OCR0B = volume_level(samples[current][position++]);
  }
}

static void timer_start(uint16_t rate){
  TCCR1A = 0;
  TCCR1B = _BV(WGM12)|_BV(CS11);    // CTC, clk/8
  OCR1A = TIMER1_CLOCK / rate - 1;
  TCNT1 = 0;
  TIFR1 = _BV(OCF1A);
  TIMSK1 = _BV(OCIE1A);
}

static void timer_stop(void){
  TIMSK1 = 0;
  TCCR1B = 0;
  mode = MODE_OFF;
}

// speaker: fast PWM 62.5 kHz on OC0B, the transistor is closed when it is off
static void speaker_on(void){
  OCR0B = volume_level(SILENCE);
  TCCR0A = _BV(COM0B1)|_BV(WGM01)|_BV(WGM00);
  TCCR0B = _BV(CS00);
}

static void speaker_off(void){
  TCCR0A = 0;
  TCCR0B = 0;
  SET_LOW(SPEAKER_PORT, SPEAKER_PIN);
}

/********************************* files *************************************/

static uint8_t is_record(const fat16_entry_t *entry){
  return !fat16_is_dir(entry) && !strcmp_P(entry->ext, PSTR("WAV"));
}

// number of the record name "A0000001.WAV", 0 for other names
static uint32_t record_number(const uint8_t *raw){
  if(raw[0] != 'A' || memcmp_P(raw + FAT16_NAME_SIZE, PSTR("WAV"), FAT16_EXT_SIZE)) return 0;
  uint32_t number = 0;
  for(uint8_t i = 1; i <= NUMBER_DIGITS; i++){
    if(raw[i] < '0' || raw[i] > '9') return 0;
    number = number * 10 + raw[i] - '0';
  }
  return number;
}

static uint8_t max_visit(const uint8_t *record, uint32_t sec, uint16_t offset, void *ctx){
  uint32_t *max = ctx;
  if(record[0] == FAT16_RECORD_END) return 0;
  if(record[0] == FAT16_RECORD_DELETED) return 1;
  uint32_t number = record_number(record);
  if(number > *max) *max = number;
  return 1;
}

// raw name of the next record: the biggest number + 1
static uint8_t next_name(uint8_t *raw){
  uint32_t number = 0;
  fat16_dir_walk(audio_dir, sector, max_visit, &number);
  if(number >= NUMBER_MAX) return 0;
  number++;
  raw[0] = 'A';
  for(uint8_t i = NUMBER_DIGITS; i >= 1; i--){
    raw[i] = '0' + number % 10;
    number /= 10;
  }
  memcpy_P(raw + FAT16_NAME_SIZE, PSTR("WAV"), FAT16_EXT_SIZE);
  return 1;
}

static uint8_t open_audio_dir(void){
  uint8_t raw[FAT16_RAW_NAME_SIZE];
  fat16_entry_t dir;
  memcpy_P(raw, audio_dir_name, sizeof(raw));
  if(!fat16_find(FAT16_ROOT_CLUSTER, raw, &dir, sector)){
    if(fat16_mkdir(FAT16_ROOT_CLUSTER, raw, sector)) return 0;
    if(!fat16_find(FAT16_ROOT_CLUSTER, raw, &dir, sector)) return 0;
  }
  if(!fat16_is_dir(&dir)) return 0;
  audio_dir = dir.cluster;
  return 1;
}

static void put16(uint8_t *p, uint16_t value){
  p[0] = value;
  p[1] = value >> 8;
}

static void put32(uint8_t *p, uint32_t value){
  put16(p, value);
  put16(p + 2, value >> 16);
}

static uint16_t get16(const uint8_t *p){
  return p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p){
  return get16(p) | ((uint32_t)get16(p + 2) << 16);
}

// WAV PCM 8 bit mono header for data_size bytes of samples
static void wav_header(uint8_t *p, uint32_t data_size){
  memcpy_P(p, PSTR("RIFF"), 4);
  put32(p + 4, WAV_HEADER_SIZE - 8 + data_size);
  memcpy_P(p + 8, PSTR("WAVEfmt "), 8);
  put32(p + 16, 16);              // fmt chunk size
  put16(p + 20, 1);               // PCM
  put16(p + 22, 1);               // mono
  put32(p + 24, SAMPLE_RATE);
  put32(p + 28, SAMPLE_RATE);     // bytes per second
  put16(p + 32, 1);               // block align
  put16(p + 34, 8);               // bits per sample
  memcpy_P(p + 36, PSTR("data"), 4);
  put32(p + 40, data_size);
}

// "mm:ss"
static void time_text(char *text, uint32_t seconds){
  uint16_t minutes = seconds / 60;
  if(minutes > 99) minutes = 99;
  text[0] = '0' + minutes / 10;
  text[1] = '0' + minutes % 10;
  text[2] = ':';
  text[3] = '0' + seconds % 60 / 10;
  text[4] = '0' + seconds % 10;
  text[5] = '\0';
}

/********************************* record ************************************/

static void show_record(const char *name, uint32_t samples_count, uint8_t level){
  char line[DISPLAY_COLS + 1];
  strcpy_P(line, PSTR("REC "));
  strcat(line, name);
  display_print_line(0, line);
  time_text(line, samples_count / SAMPLE_RATE);
  if(overrun) strcat_P(line, PSTR("  SD slow!"));
  display_print_line(1, line);
  // level bar: the peak of the signal since the last show
  uint8_t cols = (uint16_t)level * LEVEL_COLS / (SILENCE / 2);
  if(cols > LEVEL_COLS) cols = LEVEL_COLS;
  memset(line, '#', cols);
  line[cols] = '\0';
  display_print_line(2, line);
}

// the buffer to the file chain
static uint8_t write_samples(fat16_writer_t *writer, uint8_t index){
  uint8_t error = fat16_writer_sector(writer, samples[index], sector);
  done[index] = 0;
  return error;
}

static void record(void){
  fat16_writer_t writer;
  fat16_entry_t entry;
  uint8_t raw[FAT16_RAW_NAME_SIZE];
  char name[FAT16_NAME_SIZE + 1];
  uint8_t error;
  uint8_t key = NOOP;

  if(!next_name(raw)){
    ui_message(PSTR("no free number"), NULL);
    return;
  }
  memcpy(name, raw, FAT16_NAME_SIZE);
  name[FAT16_NAME_SIZE] = '\0';
  display_print_screen_P(NULL, PSTR(""), PSTR(""), PSTR("AB-stop C-cancel"));

  // the first cluster before the start: its search can take long
  fat16_writer_open(&writer);
  error = fat16_writer_prepare(&writer, sector);
  if(error){
    ui_show_error(error);
    return;
  }

  // the header is written at the end, the first buffer starts after it
  memset(samples[0], 0, WAV_HEADER_SIZE);
  current = 0;
  position = WAV_HEADER_SIZE;
  done[0] = done[1] = 0;
  overrun = 0;
  peak = 0;

  init_mic();
  // 8 bit result, ADC clock fosc/64: conversion 52 us < 125 us of a sample
  ADMUX |= _BV(ADLAR);
  ADCSRA = _BV(ADEN)|_BV(ADPS2)|_BV(ADPS1);
  ADCSRA |= _BV(ADSC);
  while(ADCSRA & _BV(ADSC));
  dc = (uint16_t)ADCH << DC_FRACTION;
  ADCSRA |= _BV(ADSC);

  ui_wait_release();
  mode = MODE_RECORD;
  timer_start(SAMPLE_RATE);

  uint32_t written = 0;       // sectors
  uint16_t shown = SHOW_INTERVAL_MS;
  error = FAT16_OK;
  while(!error){
    for(uint8_t i = 0; i < 2; i++){
      // the buffer which is not filled now
      if(done[i] && i != current){
        error = write_samples(&writer, i);
        written++;
      }
    }
    if(shown >= SHOW_INTERVAL_MS){
      uint8_t level;
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
        level = peak;
        peak = 0;
      }
      show_record(name, written * SD_SECTOR_SIZE + position - WAV_HEADER_SIZE, level);
      shown = 0;
    }
    key = keys_get_press();
    if(key == A_KEY_PRESSED || key == B_KEY_PRESSED || key == C_KEY_PRESSED) break;
    _delay_ms(POLL_INTERVAL_MS);
    shown += POLL_INTERVAL_MS;
  }
  timer_stop();

  if(key == C_KEY_PRESSED){
    fat16_free(writer.first, sector);
    ui_wait_release();
    return;
  }

  display_print_screen_P(PSTR("saving..."), NULL, PSTR(""), PSTR(""));
  display_print_line(1, name);
  // the full buffer, then the current one (zeros after the end)
  uint8_t last = current;
  if(!error && done[last ^ 1]){
    error = write_samples(&writer, last ^ 1);
    written++;
  }
  uint32_t size = written * SD_SECTOR_SIZE + position;
  if(!error && position){
    memset(samples[last] + position, 0, SD_SECTOR_SIZE - position);
    error = write_samples(&writer, last);
  }

  // the header in the first sector of the file
  if(!error){
    uint32_t first = fat16_cluster_sector(writer.first);
    if(!sd_read_sector(first, sector)){
      error = FAT16_ERROR_IO;
    }else{
      wav_header(sector, size - WAV_HEADER_SIZE);
      if(!sd_write_sector(first, sector)) error = FAT16_ERROR_IO;
    }
  }
  if(!error) error = fat16_add_file(audio_dir, raw, writer.first, size, &entry, sector);
  if(error){
    fat16_free(writer.first, sector);
    ui_show_error(error);
  }
  ui_wait_release();
}

/********************************** play *************************************/

typedef struct {
  fat16_entry_t entry;
  fat16_seek_t seek;
  uint32_t data_start;        // offset of the samples in the file
  uint32_t data_size;
  uint16_t rate;
  uint32_t next;              // offset of the next sector to read
  uint32_t buffer_offset[2];  // file offset of the buffers
} player_t;

static player_t player;

// WAV PCM 8 bit mono: the place of the samples and the rate
static uint8_t wav_open(void){
  fat16_entry_t *entry = &player.entry;
  uint32_t place;
  player.seek.cluster = 0;
  if(entry->size < WAV_HEADER_SIZE) return 0;
  if(!fat16_file_sector(entry->cluster, 0, &player.seek, &place, sector)) return 0;
  if(!sd_read_sector(place, sector)) return 0;
  if(memcmp_P(sector, PSTR("RIFF"), 4) || memcmp_P(sector + 8, PSTR("WAVE"), 4)) return 0;

  // chunks in the first sector
  uint8_t has_format = 0;
  uint16_t offset = 12;
  while(offset + 8 <= SD_SECTOR_SIZE){
    uint32_t chunk = get32(sector + offset + 4);
    if(!memcmp_P(sector + offset, PSTR("fmt "), 4)){
      if(get16(sector + offset + 8) != 1 || get16(sector + offset + 10) != 1
          || get16(sector + offset + 22) != 8) return 0;
      uint32_t rate = get32(sector + offset + 12);
      if(rate < SAMPLE_RATE_MIN || rate > SAMPLE_RATE_MAX) return 0;
      player.rate = rate;
      has_format = 1;
    }else if(!memcmp_P(sector + offset, PSTR("data"), 4)){
      if(!has_format) return 0;
      player.data_start = offset + 8;
      player.data_size = chunk;
      // the size in the header can be wrong (record was not finished)
      if(player.data_start + player.data_size > entry->size){
        player.data_size = entry->size - player.data_start;
      }
      return 1;
    }
    // chunks are word aligned
    offset += 8 + chunk + (chunk & 1);
  }
  return 0;
}

// the next sector of the file to the buffer, length 0 after the end
static uint8_t player_fill(uint8_t index){
  uint32_t end = player.data_start + player.data_size;
  uint16_t len = 0;
  uint32_t offset = player.next;
  if(offset < end){
    uint32_t place;
    uint32_t sector_offset = offset & ~(uint32_t)(SD_SECTOR_SIZE - 1);
    if(!fat16_file_sector(player.entry.cluster, sector_offset / SD_SECTOR_SIZE,
                          &player.seek, &place, sector)) return 0;
    if(!sd_read_sector(place, samples[index])) return 0;
    // the start of the data can be in the middle of the sector
    uint16_t skip = offset - sector_offset;
    uint32_t left = end - sector_offset;
    len = left < SD_SECTOR_SIZE ? left : SD_SECTOR_SIZE;
    if(skip){
      memmove(samples[index], samples[index] + skip, len - skip);
      len -= skip;
    }
    player.next = sector_offset + SD_SECTOR_SIZE;
  }
  player.buffer_offset[index] = offset;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
    length[index] = len;
    done[index] = 0;
  }
  return 1;
}

// played samples
static uint32_t player_position(void){
  uint32_t offset;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
    offset = player.buffer_offset[current] + position;
  }
  return offset - player.data_start;
}

// playing from the sample, the interrupt is stopped while the buffers change
static uint8_t player_seek(uint32_t sample, uint8_t playing){
  if(sample > player.data_size) sample = player.data_size;
  timer_stop();
  player.next = player.data_start + sample;
  current = 0;
  position = 0;
  if(!player_fill(0) || !player_fill(1)) return 0;
  if(playing){
    mode = MODE_PLAY;
    timer_start(player.rate);
  }
  return 1;
}

static void show_player(uint32_t sample, uint8_t paused){
  char line[DISPLAY_COLS + 1];
  char total[6];
  display_print_line(0, player.entry.name);

  time_text(line, sample / player.rate);
  strcat_P(line, PSTR(" / "));
  time_text(total, player.data_size / player.rate);
  strcat(line, total);
  display_print_line(1, line);

  // progress bar
  uint32_t per_col = player.data_size / DISPLAY_COLS;
  uint8_t cols = per_col ? sample / per_col : DISPLAY_COLS;
  if(cols > DISPLAY_COLS) cols = DISPLAY_COLS;
  memset(line, '#', cols);
  memset(line + cols, '.', DISPLAY_COLS - cols);
  line[DISPLAY_COLS] = '\0';
  display_print_line(2, line);

  // "play  vol ###."
  if(sample >= player.data_size){
    strcpy_P(line, PSTR("end:A "));
  }else if(paused){
    strcpy_P(line, PSTR("pause "));
  }else{
    strcpy_P(line, PSTR("play  "));
  }
  strcat_P(line, PSTR("vol "));
  uint8_t len = strlen(line);
  for(uint8_t i = 0; i < VOLUME_MAX; i++){
    line[len + i] = i < volume ? '#' : '.';
  }
  line[len + VOLUME_MAX] = '\0';
  display_print_line(3, line);
}

// result of play: the list goes to the previous / next record
#define PLAY_BACK       0
#define PLAY_PREVIOUS   1
#define PLAY_NEXT       2

static uint8_t play(const fat16_entry_t *entry){
  uint8_t result = PLAY_BACK;
  uint8_t paused = 0;
  uint8_t ok;

  player.entry = *entry;
  display_print_screen_P(PSTR("reading..."), NULL, PSTR(""), PSTR(""));
  display_print_line(1, entry->name);
  if(!wav_open()){
    ui_message(PSTR("not WAV 8 bit"), entry->name);
    return PLAY_BACK;
  }

  speaker_on();
  ok = player_seek(0, 1);
  uint16_t shown = SHOW_INTERVAL_MS;
  uint32_t seek_step = (uint32_t)SEEK_SECONDS * player.rate;
  ui_wait_release();
  while(ok){
    for(uint8_t i = 0; i < 2 && ok; i++){
      if(done[i]) ok = player_fill(i);
    }
    uint32_t sample = player_position();
    uint8_t at_end = sample >= player.data_size;
    if(shown >= SHOW_INTERVAL_MS){
      show_player(sample, paused);
      shown = 0;
    }

    uint8_t key = keys_get_repeat(POLL_INTERVAL_MS);
    if(key != NOOP) shown = SHOW_INTERVAL_MS;
    switch(key){
      case A_KEY_PRESSED:
        if(at_end){
          paused = 0;
          speaker_on();
          ok = player_seek(0, 1);
        }else if(paused){
          paused = 0;
          speaker_on();
          mode = MODE_PLAY;
          timer_start(player.rate);
        }else{
          // no current through the speaker while it is silent
          paused = 1;
          timer_stop();
          speaker_off();
        }
        break;
      case LEFT_KEY_PRESSED:
        ok = player_seek(sample > seek_step ? sample - seek_step : 0, !paused);
        break;
      case RIGHT_KEY_PRESSED:
        ok = player_seek(sample + seek_step, !paused);
        break;
      case B_KEY_PRESSED:
        // 1..VOLUME_MAX round, the volume stays for the next records
        volume = volume % VOLUME_MAX + 1;
        break;
      case UP_KEY_PRESSED:
        result = PLAY_PREVIOUS;
        goto stop;
      case DOWN_KEY_PRESSED:
        result = PLAY_NEXT;
        goto stop;
      case C_KEY_PRESSED:
        goto stop;
    }
    _delay_ms(POLL_INTERVAL_MS);
    shown += POLL_INTERVAL_MS;
  }
  ui_message(PSTR("SD card error"), NULL);

stop:
  timer_stop();
  speaker_off();
  ui_wait_release();
  return result;
}

/********************************** list *************************************/

static uint16_t count;
static uint16_t selected;
static uint16_t first;

static uint8_t read_entry(uint16_t index, fat16_entry_t *entry){
  return fat16_dir_read(audio_dir, index, entry, 1, sector) == 1;
}

static void show_list(void){
  display_print_line_P(0, PSTR("B-rec A-play"));
  if(!count){
    display_print_line_P(1, PSTR(" no records"));
    display_print_line_P(2, PSTR(""));
    display_print_line_P(3, PSTR(""));
    return;
  }
  if(selected < first) first = selected;
  if(selected >= first + LIST_ROWS) first = selected - LIST_ROWS + 1;
  for(uint8_t row = 0; row < LIST_ROWS; row++){
    char line[DISPLAY_COLS + 1];
    fat16_entry_t entry;
    uint16_t index = first + row;
    line[0] = '\0';
    if(index < count && read_entry(index, &entry)){
      // ">A0000001 01:23"
      line[0] = index == selected ? '>' : ' ';
      strcpy(line + 1, entry.name);
      char *dot = strchr(line, '.');
      if(dot) *dot = '\0';
      if(is_record(&entry)){
        uint32_t samples_count = entry.size > WAV_HEADER_SIZE ? entry.size - WAV_HEADER_SIZE : 0;
        strcat_P(line, PSTR(" "));
        time_text(line + strlen(line), samples_count / SAMPLE_RATE);
      }
    }
    display_print_line(row + 1, line);
  }
}

static void reload(void){
  count = fat16_dir_count(audio_dir, sector);
  if(count && selected >= count) selected = count - 1;
}

static void delete_selected(void){
  fat16_entry_t entry;
  if(!count || !read_entry(selected, &entry)) return;
  if(!ui_sure(PSTR("delete?"), entry.name)) return;
  ui_show_error(fat16_delete(&entry, sector));
  reload();
}

static void run_menu(void){
  static const char items[MENU_ITEMS][8] PROGMEM = {"delete", "exit"};
  ui_wait_release();
  switch(ui_menu((const char *)items, sizeof(items[0]), MENU_ITEMS)){
    case MENU_DELETE:
      delete_selected();
      break;
    case MENU_EXIT:
      if(!ui_exit_confirm()) break;
      if(!loader_is_present()){
        ui_message(PSTR("no bootloader"), NULL);
        break;
      }
      loader_load_default_app();
  }
  ui_wait_release();
}

// plays the selected record, UP / DOWN in the player go along the list
static void play_selected(void){
  while(count){
    fat16_entry_t entry;
    if(!read_entry(selected, &entry) || !is_record(&entry)) return;
    uint8_t result = play(&entry);
    if(result == PLAY_PREVIOUS && selected > 0){
      selected--;
    }else if(result == PLAY_NEXT && selected + 1 < count){
      selected++;
    }else{
      return;
    }
  }
}

/********************************** main *************************************/

int main(void){
  init_keys();
  // CSN high before any SPI traffic, otherwise radio catches display data
  init_radio();
  init_sd();
  init_display();
  SET_DDR_OUT(SPEAKER_DDR, SPEAKER_PIN);
  speaker_off();
  sei();

  while(1){
    ui_mount(sector);
    SPI_1MHZ();
    if(open_audio_dir()) break;
    ui_message(PSTR("no dir /AUDIO"), NULL);
  }
  reload();
  show_list();

  while(1){
    uint8_t key = keys_get_repeat(POLL_INTERVAL_MS);
    if(key == NOOP){
      _delay_ms(POLL_INTERVAL_MS);
      continue;
    }
    switch(key){
      case UP_KEY_PRESSED:
        if(selected) selected--;
        break;
      case DOWN_KEY_PRESSED:
        if(selected + 1 < count) selected++;
        break;
      case A_KEY_PRESSED:
        play_selected();
        break;
      case B_KEY_PRESSED:
        record();
        reload();
        // the new record is the last one
        if(count) selected = count - 1;
        break;
      case C_KEY_PRESSED:
        run_menu();
        break;
    }
    show_list();
  }
}
