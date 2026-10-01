#include "sys.h"
#include <avr/sleep.h>
#include "pda.h"
#include "frame.h"
#include "key.h"
#include "oled.h"
#include "eep.h"

// the hardware of the PDA: Timer2 counts millis (it is not powered off)
Arduboy arduboy;

//---------------------------------------------------------------------------
void SysInit(void)
{
	OledInit();
	FrameInit();
	KeyInit();
	EepInit();
}
//---------------------------------------------------------------------------
void SysIdle(void)
{
	set_sleep_mode(SLEEP_MODE_IDLE);
	sleep_mode();
}
//---------------------------------------------------------------------------
bool SysLoop(void)
{
	if(FrameLoop() == FALSE)
	{
		return FALSE;
	}

	KeyLoop();
	return TRUE;
}
//---------------------------------------------------------------------------
void SysLoopEnd(void)
{
	OledDisplay();
	OledDrawCls();
}
