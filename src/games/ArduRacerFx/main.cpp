// Arduino sketch ArduRacerFx.ino: Arduino.h and main() are added by hand
#include "Arduino.h"
#define ABG_IMPLEMENTATION
#include "racer.h"

void setup()
{
  racerSetup();
}

void loop()
{
  racerLoop();
}

int main(void)
{
  setup();
  while(1) loop();
}
