/*
 *	Dynamic extension for kme that displays value-under-mask pairs.
 *
 *	!valmask32	4 byte value followed by 4 byte mask (display hex)
 *	!valmask32b	4 byte value followed by 4 byte mask (display binary)
 *	!valmask16	2 byte value followed by 2 byte mask (display hex)
 *	!valmask16b	2 byte value followed by 2 byte mask (display binary)
 *	!valmask8	1 byte value followed by 1 byte mask (display binary)
 *	!valmask8b	1 byte value followed by 1 byte mask (display binary)
 *
 * 	Example:
 * 		If you have a long value at location 0 with value 0x12345678
 * 		and a long mask at location 4 with value 0xF000FFFF, then
 * 		0/!valmask32 will display:
 * 			1xxx5678
 * 		and 0/!valmask32b will display:
 * 			0001xxxxxxxxxxxx0101011001111000
 *
 * 	Written by Rick Richardson 6/2/2001.  Donated to the public domain.
 * 	Do what you will with this.  No warrantees.
 */
typedef unsigned long ulong;
typedef unsigned short ushort;
typedef unsigned char uchar;

extern unsigned char mem[];

extern int getmem(int);
extern void add_format(char *name, char *unused,
		char *(*dlfunc)(ulong *addrp, int dlarg), int arg);

static char
hex(int val)
{
	if (val >= 0 && val <= 9)
		return (val + '0');
	else
		return (val - 10 + 'A');
}

static char *
valmask32(ulong *addrp, int arg)
{
	ulong		val, mask;
	static char	fmt[64];
	int		i, s;

	if (!getmem(8))
		return " \"????\" ";

	val = ((ulong *) mem)[0];
	mask = ((ulong *) mem)[1];

	fmt[0] = '"';
	for (i = 1, s = (32-4); i <= 8; ++i, s-=4)
	{
		if ( ((mask>>s) & 0x0f) == 0)
			fmt[i] = 'x';
		else if ( ((mask>>s) & 0x0f) == 15)
			fmt[i] = hex((val>>s) & 0x0f);
		else
			fmt[i] = '?';
	}
	fmt[8+1] = '"';
	fmt[8+2] = '8';
	fmt[8+3] = '+';
	fmt[8+4] = 0;

	return fmt;
}

static char *
valmask32b(ulong *addrp, int arg)
{
	ulong		val, mask;
	static char	fmt[64];
	int		i, s;

	if (!getmem(8))
		return " \"????\" ";

	val = ((ulong *) mem)[0];
	mask = ((ulong *) mem)[1];

	fmt[0] = '"';
	for (i = 1, s = (32-1); i <= 32; ++i, s -= 1)
	{
		if ( ((mask>>s) & 0x01) == 0)
			fmt[i] = 'x';
		else if ( (val>>s) & 0x01 )
			fmt[i] = '1';
		else
			fmt[i] = '0';
	}
	fmt[32+1] = '"';
	fmt[32+2] = '8';
	fmt[32+3] = '+';
	fmt[32+4] = 0;

	return fmt;
}

static char *
valmask16(ulong *addrp, int arg)
{
	ushort		val, mask;
	static char	fmt[64];
	int		i, s;

	if (!getmem(4))
		return " \"????\" ";

	val = ((ushort *) mem)[0];
	mask = ((ushort *) mem)[1];

	fmt[0] = '"';
	for (i = 1, s = (16-4); i <= 4; ++i, s -= 4)
	{
		if ( ((mask>>s) & 0x0f) == 0)
			fmt[i] = 'x';
		else if ( ((mask>>s) & 0x0f) == 15)
			fmt[i] = hex((val>>s) & 0x0f);
		else
			fmt[i] = '?';
	}
	fmt[4+1] = '"';
	fmt[4+2] = '4';
	fmt[4+3] = '+';
	fmt[4+4] = 0;

	return fmt;
}

static char *
valmask16b(ulong *addrp, int arg)
{
	ushort		val, mask;
	static char	fmt[64];
	int		i, s;

	if (!getmem(4))
		return " \"????\" ";

	val = ((ushort *) mem)[0];
	mask = ((ushort *) mem)[1];

	fmt[0] = '"';
	for (i = 1, s = (16-1); i <= 16; ++i, s -= 1)
	{
		if ( ((mask>>s) & 0x01) == 0)
			fmt[i] = 'x';
		else if ( (val>>s) & 0x01 )
			fmt[i] = '1';
		else
			fmt[i] = '0';
	}
	fmt[16+1] = '"';
	fmt[16+2] = '4';
	fmt[16+3] = '+';
	fmt[16+4] = 0;

	return fmt;
}

static char *
valmask8(ulong *addrp, int arg)
{
	uchar		val, mask;
	static char	fmt[64];
	int		i, s;

	if (!getmem(2))
		return " \"????\" ";

	val = ((uchar *) mem)[0];
	mask = ((uchar *) mem)[1];

	fmt[0] = '"';
	for (i = 1, s = (8-1); i <= 8; ++i, s -= 1)
	{
		if ( ((mask>>s) & 0x01) == 0)
			fmt[i] = 'x';
		else if ( (val>>s) & 0x01 )
			fmt[i] = '1';
		else
			fmt[i] = '0';
	}
	fmt[8+1] = '"';
	fmt[8+2] = '2';
	fmt[8+3] = '+';
	fmt[8+4] = 0;

	return fmt;
}

void
_init(void)
{
	#define NULL 0

	add_format("valmask32", NULL, &valmask32, 0);
	add_format("valmask32b", NULL, &valmask32b, 0);
	add_format("valmask16", NULL, &valmask16, 0);
	add_format("valmask16b", NULL, &valmask16b, 0);
	add_format("valmask8", NULL, &valmask8, 0);
	add_format("valmask8b", NULL, &valmask8, 0);
}
