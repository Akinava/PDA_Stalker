// Arduino sketch CastleBoy.ino: Arduino.h and main() are added by hand
#include "Arduino.h"
#include "global.h"

#include "menu.h"
#include "game.h"
#include "assets.h"

uint8_t bootCounter = 0;

void setup();
void loop();

int main(void)
{
  setup();
  while(1) loop();
}

void setup()
{
  ab.begin();
  ab.setFrameRate(FPS);

  Menu::showTitle();
}

void loop()
{
  if (!ab.nextFrame())
  {
    return;
  }

  ab.clear();
  
  if(bootCounter < 120)
  {
    bootCounter++;
    sprites.drawOverwrite(49, 14, logo, 0);
    ab.display();
    return;
  }

  ab.pollButtons();
  Menu::loop();

#ifdef DEBUG_LOG
  drawDebugLog();
#endif

#ifdef DEBUG_CPU
  drawDebugCpu();
#endif

#ifdef DEBUG_RAM
  drawDebugRam();
#endif

  if (flashCounter > 0)
  {
    ab.fillRect(0, 0, 128, 64, WHITE);
    flashCounter--;
  }
  ab.display();
}
