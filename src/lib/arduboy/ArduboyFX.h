#ifndef ARDUBOY_FX_H
#define ARDUBOY_FX_H

// ArduboyFX library interface: the PDA has no FX flash chip, the FX data is
// the file /GAMES/<ARDUBOY_FX_NAME> on the SD card (fxdata-data.bin of the game).
// The file must be in one piece on the card (not fragmented).
// The sectors are read byte by byte without buffer: the card stays selected
// between reads and is released before the display uses the SPI bus.
// The FX save area is in EEPROM at ARDUBOY_FX_SAVE_ADDRESS.

#include "Arduboy.h"

using uint24_t = __uint24;

// FAT name of the data file: 8 + 3 chars, padded with spaces
#ifndef ARDUBOY_FX_NAME
#define ARDUBOY_FX_NAME "FXDATA  DAT"
#endif

#ifndef ARDUBOY_FX_SAVE_ADDRESS
#define ARDUBOY_FX_SAVE_ADDRESS 900
#endif

// drawBitmap modes (ArduboyFX), only these two are supported
constexpr uint8_t dbfMasked = 4;
constexpr uint8_t dbmNormal = 0;
constexpr uint8_t dbmOverwrite = 0;
constexpr uint8_t dbmMasked = _BV(dbfMasked);

constexpr bool CLEAR_BUFFER = true;

namespace FX {
  // the pages of the FX chip are not used: the data is the file
  void begin(uint16_t dataPage = 0, uint16_t savePage = 0);

  void seekData(uint24_t address);
  uint8_t readPendingUInt8(void);
  uint16_t readPendingUInt16(void);
  uint16_t readPendingLastUInt16(void);
  uint24_t readPendingUInt24(void);
  uint32_t readPendingUInt32(void);
  // the last byte of the read; the card is released lazily
  // (the next seek can go on in the same sector)
  uint8_t readEnd(void);

  uint8_t readIndexedUInt8(uint24_t address, uint8_t index);
  uint16_t readIndexedUInt16(uint24_t address, uint8_t index);
  uint24_t readIndexedUInt24(uint24_t address, uint8_t index);
  uint32_t readIndexedUInt32(uint24_t address, uint8_t index);

  // FX image: width, height (big endian 16 bit), frames of vertical bytes,
  // dbmMasked: image and mask bytes are interleaved
  void drawBitmap(int16_t x, int16_t y, uint24_t address, uint8_t frame, uint8_t mode);

  void display(void);
  void display(bool clear);

  // the card does not hold the bus (called by the display)
  void release(void);

  // save: "FX", size, data
  template<typename T> void saveGameState(const T &state){
    static_assert(ARDUBOY_FX_SAVE_ADDRESS + 3 + sizeof(T) <= E2END + 1, "FX save does not fit in EEPROM");
    EEPROM.update(ARDUBOY_FX_SAVE_ADDRESS, 'F');
    EEPROM.update(ARDUBOY_FX_SAVE_ADDRESS + 1, 'X');
    EEPROM.update(ARDUBOY_FX_SAVE_ADDRESS + 2, sizeof(T));
    EEPROM.put(ARDUBOY_FX_SAVE_ADDRESS + 3, state);
  }

  // returns 1 if the state is loaded, 0: state is not changed
  template<typename T> uint8_t loadGameState(T &state){
    if(EEPROM.read(ARDUBOY_FX_SAVE_ADDRESS) != 'F'
        || EEPROM.read(ARDUBOY_FX_SAVE_ADDRESS + 1) != 'X'
        || EEPROM.read(ARDUBOY_FX_SAVE_ADDRESS + 2) != sizeof(T)) return 0;
    EEPROM.get(ARDUBOY_FX_SAVE_ADDRESS + 3, state);
    return 1;
  }
}

#endif
