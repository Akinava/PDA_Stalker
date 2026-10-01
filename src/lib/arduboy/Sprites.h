#ifndef ARDUBOY_SPRITES_H
#define ARDUBOY_SPRITES_H

// Sprites class of Arduboy2. Sprite: width, height, then frames of
// vertical bytes ((height + 7) / 8 rows of width bytes).
// Plus mask sprite: image and mask bytes are interleaved

#include "Arduboy.h"

#define SPRITE_MASKED       1
#define SPRITE_UNMASKED     2
#define SPRITE_OVERWRITE    2
#define SPRITE_PLUS_MASK    3
#define SPRITE_IS_MASK      250
#define SPRITE_IS_MASK_ERASE 251
#define SPRITE_AUTO_MODE    255

class Sprites {
  public:
    // mask has no width and height bytes
    static void drawExternalMask(int16_t x, int16_t y, const uint8_t *bitmap,
                                 const uint8_t *mask, uint8_t frame, uint8_t mask_frame);
    static void drawPlusMask(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);
    static void drawOverwrite(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);
    static void drawErase(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);
    static void drawSelfMasked(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);

    static void draw(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame,
                     const uint8_t *mask, uint8_t sprite_frame, uint8_t drawMode);
    static void drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, const uint8_t *mask,
                           uint8_t w, uint8_t h, uint8_t draw_mode);
};

#endif
