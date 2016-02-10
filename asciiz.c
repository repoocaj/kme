/*
 *	Example dynamic extension for kme
 */
#define NULL 0

extern unsigned char *mem;

typedef unsigned long ulong;

char *
format(ulong * addrp, int arg)
{
	ulong		faddr = *addrp;
	ulong		zaddr;
	static char	fmt[32];

	for (;;)
	{
		if (!getmem(1))
			return " \"????\" ";
		if (*mem == 0)
			break;
		(*addrp)++;
	}
	zaddr = *addrp;
	*addrp = faddr;

	sprintf(fmt, "%d a 1+\n", zaddr - faddr);

	return (fmt);
}

void
_init(void)
{
	add_format("asciiz", NULL, &format, 0);
}
