#include "key.h"
#include "pda.h"

//---------------------------------------------------------------------------
ST_KEY Key;


//---------------------------------------------------------------------------
void KeyInit(void)
{
	// the keys are set up by arduboy.begin (OledInit)
	_Memset(&Key, 0x00, sizeof(ST_KEY));
}
//---------------------------------------------------------------------------
void KeyLoop(void)
{
	// PDA keys, C opens the exit window
	u8 cnt = arduboy.getInput();

	Key.trg = (Key.trg ^ cnt) & ~Key.cnt;
	Key.off = ~cnt & Key.cnt;
	Key.cnt = cnt;


	if(Key.trg & KEY_ALL || Key.repCnt == 0)
	{
		Key.rep = Key.cnt;
		Key.repCnt = KEY_REPEAT_CNT;
	}
	else
	{
		Key.rep = 0;
	}

	if(Key.cnt & KEY_ALL)
	{
		if(Key.repCnt != 0) Key.repCnt--;
	}
	else
	{
		Key.repCnt = 0;
	}
}
//---------------------------------------------------------------------------
u8 KeyGetCnt(void)
{
	return Key.cnt;
}
//---------------------------------------------------------------------------
u8 KeyGetTrg(void)
{
	return Key.trg;
}
//---------------------------------------------------------------------------
u8 KeyGetOff(void)
{
	return Key.off;
}
//---------------------------------------------------------------------------
u8 KeyGetRep(void)
{
	return Key.rep;
}
