/*
 * KME helpers to read ELF core files.       
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"


#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include "elfcore.h"

#if HAVE_UNISTD_H
#  include <unistd.h>
#endif

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#if HAVE_LIBELF

#include <libelf.h>

static Elf32_Phdr *phdr;
static Elf32_Ehdr *ehdr;
static int elf_fd;

/*
 * Internal initializations when 'read from corefile' is selected.
 */
int
read_corefile(const char *fname)
{
	Elf *elf;

	elf_fd = open(fname, O_RDONLY);
	if (elf_version(EV_CURRENT)  == EV_NONE) {
		fprintf(stderr, "Elf library out of sync\n");
		return 1;
	}

	elf = elf_begin(elf_fd, ELF_C_READ, NULL);

	ehdr = elf32_getehdr(elf);
	phdr = elf32_getphdr(elf);

	if (ehdr == NULL || phdr == NULL) {
		return 1;
	}

	return 0;
}

/*
 * Given a start/length pair, return the number of successive bytes
 * that can be read (can be less than requested if near the end of a 
 * phdr) and the offset in the source file whence it can be found.
 */
long 
find_in_corefile(long addr, long ct, off_t *offset)
{
	int i;
	Elf32_Addr eaddr = (Elf32_Addr) addr;
	Elf32_Phdr *pp = phdr;

	for (i = 0; i < ehdr->e_phnum; i++, pp++) {
		if ((eaddr >= pp->p_vaddr ) && 
			(eaddr <= (pp->p_vaddr + pp->p_memsz))) {
			*offset =  eaddr - pp->p_vaddr + pp->p_offset;

			if (eaddr  + ct > pp->p_vaddr + pp->p_memsz)
				return pp->p_vaddr + pp->p_memsz - eaddr;
			else 
				return ct;
		}
	}
	return -1;
}

/*
 * Called from getmem to do the actual read of 'sz' bytes from address 'a'
 * and put them in the 'tgtmem' buffer.
 */
 
int
getmem_from_corefile(int sz, long a, void *tgmem)
{
	off_t off;
	long n;

	n = find_in_corefile(a, sz, &off);

	if (n < 0) {
		*(int *)tgmem = -1;
		return -1;
	}
	
	if (lseek(elf_fd, off, SEEK_SET) != off || 
		read(elf_fd, tgmem, n) != sz ) {
		*(int*)tgmem = errno;
		return -1;
	}

	return sz;
}

#else /* HAVE_LIBELF */

long 
find_in_corefile(long addr, long ct, off_t *offset)
{
	/* unreachable */
	abort();
}

int
getmem_from_corefile(int sz, long a, void *tgmem)
{
	/* unreachable */
	abort();
}

int
read_corefile(const char *fname)
{
	fprintf(stderr, 
		"This build of KME does not have libelf configured in.\n");
	return 1;
}

#endif /* HAVE_LIBELF */
