#include "Sprites.h"

void Sprites::drawExternalMask(int16_t x, int16_t y, const uint8_t *bitmap,
                               const uint8_t *mask, uint8_t frame, uint8_t mask_frame){
  draw(x, y, bitmap, frame, mask, mask_frame, SPRITE_MASKED);
}

void Sprites::drawPlusMask(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame){
  draw(x, y, bitmap, frame, NULL, 0, SPRITE_PLUS_MASK);
}

void Sprites::drawOverwrite(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame){
  draw(x, y, bitmap, frame, NULL, 0, SPRITE_OVERWRITE);
}

void Sprites::drawErase(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame){
  draw(x, y, bitmap, frame, NULL, 0, SPRITE_IS_MASK_ERASE);
}

void Sprites::drawSelfMasked(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame){
  draw(x, y, bitmap, frame, NULL, 0, SPRITE_IS_MASK);
}

void Sprites::draw(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame,
                   const uint8_t *mask, uint8_t sprite_frame, uint8_t drawMode){
  if(bitmap == NULL) return;

  uint8_t width = pgm_read_byte(bitmap);
  uint8_t height = pgm_read_byte(bitmap + 1);
  bitmap += 2;

  uint16_t frame_offset = width * ((height + 7) / 8);
  if(drawMode == SPRITE_PLUS_MASK){
    // image and mask: twice as much space for each frame
    frame_offset *= 2;
  }else if(mask != NULL){
    mask += sprite_frame * frame_offset;
  }
  bitmap += frame * frame_offset;

  if(drawMode == SPRITE_AUTO_MODE) drawMode = mask == NULL ? SPRITE_UNMASKED : SPRITE_MASKED;
  drawBitmap(x, y, bitmap, mask, width, height, drawMode);
}

void Sprites::drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, const uint8_t *mask,
                         uint8_t w, uint8_t h, uint8_t draw_mode){
  if(x + w <= 0 || x >= WIDTH || y + h <= 0 || y >= HEIGHT) return;

  uint8_t *buffer = Arduboy::sBuffer;
  uint8_t pages = (h + 7) / 8;
  uint8_t y_offset = y & 7;
  // arithmetic shift: page of the top sprite row, also for negative y
  int8_t first_page = y >> 3;

  for(uint8_t p = 0; p < pages; p++){
    int8_t page = first_page + p;
    for(uint8_t i = 0; i < w; i++){
      int16_t column = x + i;
      if(column < 0 || column >= WIDTH) continue;

      uint16_t offset = p * w + i;
      uint8_t image, image_mask;
      switch(draw_mode){
        case SPRITE_PLUS_MASK:
          image = pgm_read_byte(bitmap + 2 * offset);
          image_mask = pgm_read_byte(bitmap + 2 * offset + 1);
          break;
        case SPRITE_MASKED:
          image = pgm_read_byte(bitmap + offset);
          image_mask = pgm_read_byte(mask + offset);
          break;
        case SPRITE_UNMASKED:
          image = pgm_read_byte(bitmap + offset);
          image_mask = 0xFF;
          break;
        case SPRITE_IS_MASK_ERASE:
          // set bits of the image become black
          image = 0;
          image_mask = pgm_read_byte(bitmap + offset);
          break;
        default:    // SPRITE_IS_MASK: set bits become white
          image = pgm_read_byte(bitmap + offset);
          image_mask = image;
          break;
      }

      uint16_t image16 = (uint16_t)image << y_offset;
      uint16_t mask16 = (uint16_t)image_mask << y_offset;
      // as in Arduboy2: the image is ORed after the mask is cleared
      if(page >= 0 && page < HEIGHT / 8){
        uint8_t *b = &buffer[page * WIDTH + column];
        *b = (*b & ~(uint8_t)mask16) | (uint8_t)image16;
      }
      if(y_offset && page + 1 >= 0 && page + 1 < HEIGHT / 8){
        uint8_t *b = &buffer[(page + 1) * WIDTH + column];
        *b = (*b & ~(uint8_t)(mask16 >> 8)) | (uint8_t)(image16 >> 8);
      }
    }
  }
}
