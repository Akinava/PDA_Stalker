// Arduino sketch: Arduino.h and main() are added by hand
#include "Arduino.h"
#include "ino.h"
#include "lib/sys.h"
#include "game.h"

//---------------------------------------------------------------------------
void setup(void)
{
	SysInit();

	GameInit();
}
//---------------------------------------------------------------------------
void loop(void)
{
	if(SysLoop() == FALSE)
	{
		return;
	}

	GameLoop();

	SysLoopEnd();
}
//---------------------------------------------------------------------------
int main(void)
{
	setup();
	while(1) loop();
}
