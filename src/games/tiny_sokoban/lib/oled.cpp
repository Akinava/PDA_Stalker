#include "oled.h"
#include <stdarg.h>
#include "pda.h"
#include "../res/font.h"

// SSD1306 OLED

//---------------------------------------------------------------------------
ST_OLED Oled;


//---------------------------------------------------------------------------
void OledInit(void)
{
	arduboy.begin();
	Oled.buf = arduboy.getBuffer();
}
//---------------------------------------------------------------------------
void OledDisplay(void)
{
	arduboy.display();
}
//---------------------------------------------------------------------------
void OledDrawStr(u8 fx, u8 fy, const char* fmt, ...)
{
	char s[40];

	va_list ap;
	va_start(ap, fmt);
	_SprintfDo(s, fmt, ap);
	va_end(ap);


	u8 i;

	for(i=0; s[i] != '\0'; i++)
	{
		OledDrawChr(fx++, fy, s[i]);
	}
}
//---------------------------------------------------------------------------
void OledDrawChr(u8 fx, u8 fy, char chr)
{
	if(fx >= OLED_SCREEN_FONT_CX || fy >= OLED_SCREEN_FONT_CY)
	{
		return;
	}

	u8 x;

	for(x=0; x<OLED_FONT_SIZE; x++)
	{
		Oled.buf[(fx * OLED_FONT_CX + x) + (fy * OLED_SCREEN_CX)] = __LPM(font + (chr * OLED_FONT_SIZE) + x);
	}
}
//---------------------------------------------------------------------------
void OledDrawDot(u8 x, u8 y)
{
	Oled.buf[x + (y / 8) * OLED_SCREEN_CX] |= _BV(y % 8);
}
//---------------------------------------------------------------------------
void OledDrawCls(void)
{
//	u16 i;
//	for(i=0; i<sizeof(Oled.buf); i++) Oled.buf[i] = 0x00;

	asm volatile(
		"movw  r30, %0                \n\t"
		"eor __tmp_reg__, __tmp_reg__ \n\t"
		"loop:                        \n\t"
		"st Z+, __zero_reg__          \n\t"
		"st Z+, __zero_reg__          \n\t"
		"st Z+, __zero_reg__          \n\t"
		"st Z+, __zero_reg__          \n\t"
		"inc __tmp_reg__              \n\t"
		"brne loop                    \n\t"

		: : "r" (Oled.buf) : "r30","r31"
	);
}
//---------------------------------------------------------------------------
void OledDrawBmp(s8 sx, s8 sy, u8* p)
{
	u8* d  = (u8*)p;
	u8  cx = __LPM(d++);
	u8  cy = __LPM(d++);

	u8 chr, mask;
	u8 x, y, b;

	for(y=0; y<cy; y++)
	{
		if(sy + y < 0 || sy + y >= OLED_SCREEN_CY)
		{
			d += cx / 8;
			continue;
		}

		for(x=0; x<cx; x+=8)
		{
			chr  = __LPM(d++);
			mask = 0x80;

			for(b=0; b<8; b++)
			{
				if(sx + x + b >= 0 && sx + x + b < OLED_SCREEN_CX)
				{
					if(chr & mask)
					{
						Oled.buf[(sx + x + b) + ((sy + y) / 8) * OLED_SCREEN_CX] |= _BV((sy + y) & 0x7);
					}
				}

			 	mask >>=1;
			}
		}
	}
}
//---------------------------------------------------------------------------
void OledDrawPng8(s8 bx, s8 by, u8* p, u8 num)
{
	u8* d = (u8*)p + num * 8;
	u8  i;

	for(i=0; i<8; i++)
	{
		Oled.buf[(bx * 8 + by * OLED_SCREEN_CX) + i] |= __LPM(d + i);
	}
}
//---------------------------------------------------------------------------
void OledDrawBlock(s8 bx, s8 by, u8* p)
{
	OledDrawBmp(64 + bx * 8, by * 8, p);
}
//---------------------------------------------------------------------------
void OledDrawBlock2(s8 x, s8 y, u8* p)
{
	OledDrawBmp(64 + x, y, p);
}
//---------------------------------------------------------------------------
void OledDrawBlockWall(s8 bx, s8 by, u8* p, u8 num)
{
	OledDrawPng8(8 + bx, by, p, num);
}
