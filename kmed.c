/*
 * This is a sample kme UDP server for implementing
 * remote kme.  In this case, we interface to the
 * linux device driver for HLC by default.
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

#if !defined(HAVE_SOCKET)
#error kmed requires the system socket() call.
#endif

#if HAVE_UNISTD_H
#  include <unistd.h>
#endif

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#if HAVE_STRING_H
#  include <string.h>
#endif

#if HAVE_STRINGS_H
#  include <strings.h>
#endif

#if HAVE_GETOPT_H
#  include <getopt.h>
#endif

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#if HAVE_MMAP 
#  include <sys/mman.h>
#endif

#include "kme.h"

char kmed_c_version[] = "@Id$";

/*
 *  Unsigned variables.  This form makes them impervious
 *  to similar defs in sys/types.h.
 */

#define uchar unsigned char
#define ushort unsigned short
#define ulong unsigned long

/*
 *	The name of the memory device(s) to use by default
 *
 *	May consist of several device/file names separated by colons.
 *	This allows you to select one of several different memory
 *	spaces remotely.
 *
 *	In addition, the device name may be suffixed by ",offset,size",
 *	which causes the device to be mmap'ed into this process.
 */

char	*MemName = "/dev/kmem";		/* Kernel memory */

typedef struct
{
    char	name[256];
    ulong	mmap_addr;
    ulong	mmap_size;

    int		fd;
    int		warned;
    ulong	mmap_curaddr;
    void	*mmap_vaddr;
} COREINFO;

#define	NUMCORE	16
COREINFO	CoreInfo[NUMCORE];
int		NumCore = 0;

/*
 *	The current rw_t struct leaves the endianness of rw_size and
 *	rw_addr undefined.  This is not good.  We can run this server
 *	in three modes to try to deal with this.
 */

#define	RW_ASIS			0	/* Pray its right already */
#define	RW_IN_NETWORK_ORDER	1	/* Assume its in network order */
#define	RW_DETECT		2	/* Guess, based on rw_size */

int	RwEndianness = RW_DETECT;

int	ReadOnly = 0;

int	Debug = 0;

/*
 *	Create coreinfo data
 */
void
coreinfo(char *str)
{
    int i;
    int len;
    char *start = str;
    char *end;
    
    for (NumCore = 0; NumCore < NUMCORE; )
    {
	end = strchr(start, ':');
	if (!end) end = strchr(start, 0);

	if (end - start == 2 && start[2] == '\\') {
	    end = index(start+3, ':');
	    if (!end) end = strchr(start+3, 0);
	}

	len = end - start;
	if (len >= sizeof(CoreInfo[NumCore].name))
	    len = sizeof(CoreInfo[NumCore].name) - 1;

	strncpy(CoreInfo[NumCore].name, start, len);
	CoreInfo[NumCore].name[len] = 0;

	start = strchr(CoreInfo[NumCore].name, ',');
	if (start)
	{
	    #if !HAVE_MMAP
		fprintf("Sorry, no mmap support on this target\n");
		exit(1);
	    #endif
	    *start++ = 0;
	    CoreInfo[NumCore].mmap_addr = strtoul(start, 0, 0);
	    start = strchr(start, ',');
	    if (start)
		CoreInfo[NumCore].mmap_size = strtoul(start+1, 0, 0);
	}

	CoreInfo[NumCore].fd = -1;
	CoreInfo[NumCore].mmap_curaddr = 0;
	CoreInfo[NumCore].mmap_vaddr = (void *) -1;
	++NumCore;

	if (*end == 0)
	    break;
	else
	    start = end+1;
    }
    if (Debug > 1)
	for (i = 0; i < NumCore; ++i)
	{
	    fprintf(stderr, "%d	'%s'	%lu	%lu\n",
		i, CoreInfo[i].name,
		CoreInfo[i].mmap_addr,
		CoreInfo[i].mmap_size);
	}
}

void *
map_range(COREINFO *cp, ulong addr, ulong size)
{
    /*
     * Open the memory device if it isn't already open
     */
    if (cp->fd < 0)
    {
	cp->fd = open(cp->name, ReadOnly ? O_RDONLY : O_RDWR);
	if (cp->fd < 0)
	{
	    if (!cp->warned)
	    {
		perror("corefile open");
		cp->warned = 1;
	    }
	    return (void *) -1;
	}
    }

    /*
     * Check to see if mmap() is desired,
     */
    if (cp->mmap_size == 0)
	return (void *) -1;

    #if HAVE_MMAP
	if (cp->mmap_addr == 1)
	{
	    /*
	     * Floating mmap requested
	     *
	     * This is a simplistic implementation
	     */
	    ulong baseaddr;
	    ulong offset;

	    baseaddr = addr & ~4095;
	    offset = addr - baseaddr;
	    if ((offset+size) > cp->mmap_size)
		    return (void *) -1;
	    if (cp->mmap_vaddr == (void *) -1 || baseaddr != cp->mmap_curaddr)
	    {
		if (cp->mmap_vaddr != (void *) -1)
		{
		    if (Debug)
			fprintf(stderr, "unmap vaddr=%lx size=%lx\n",
			    (long) cp->mmap_vaddr, cp->mmap_size);
		    munmap(cp->mmap_vaddr, cp->mmap_size);
		    cp->mmap_vaddr = (void *) -1;
		}

		cp->mmap_curaddr = baseaddr;
		cp->mmap_vaddr = mmap(NULL, cp->mmap_size,
				ReadOnly ? PROT_READ : (PROT_READ|PROT_WRITE),
				MAP_SHARED, cp->fd, cp->mmap_curaddr);
		if (Debug)
		    fprintf(stderr, "new mmap vaddr=%lx addr=%lx size=%lx\n",
			    (long) cp->mmap_vaddr, cp->mmap_curaddr,
			    cp->mmap_size);
		if (cp->mmap_vaddr == ((void *) -1))
		{
		    perror("floating mmap");
		    return (void *) -1;
		}
	    }
	    if (Debug)
		fprintf(stderr, "cur=%lx size=%lx vaddr=%lx offset=%lx\n",
			(long) cp->mmap_curaddr, cp->mmap_size,
			(long) cp->mmap_vaddr, offset);
	    return cp->mmap_vaddr + offset;
	}
	else
	{
	    /*
	     * Fixed mmap requested
	     */
	    if (addr < cp->mmap_addr)
		return (void *) -1;
	    if ((addr+size) >= (cp->mmap_addr+cp->mmap_size))
		return (void *) -1;
	    
	    if (cp->mmap_vaddr == (void *) -1)
	    {
		cp->mmap_curaddr = cp->mmap_addr;
		cp->mmap_vaddr = mmap(NULL, cp->mmap_size,
				ReadOnly ? PROT_READ : (PROT_READ|PROT_WRITE),
				MAP_SHARED, cp->fd, cp->mmap_addr);
		if (cp->mmap_vaddr == ((void *) -1))
		{
		    perror("fixed mmap");
		    return (void *) -1;
		}
	    }
	    return (cp->mmap_vaddr + addr);
	}
    #else
	return (void *) -1;
    #endif
}

/*
 *	Read memory.
 */
int
read_mem(rw_t* rw)
{
    void		*vaddr;
    COREINFO		*cp;
    unsigned int	boardnum = rw->rw_module;

    if (boardnum >= NumCore)
	return 0;
    cp = &CoreInfo[boardnum];
    vaddr = map_range(cp, rw->rw_addr, rw->rw_size);
    if (vaddr == (void *) -1)
    {
	/* Not an mmap device, or need to fallback to lseek/read */
	if (cp->fd == -1)
	    return 0;
	lseek(cp->fd, (off_t) rw->rw_addr + cp->mmap_addr, 0);
	return read(cp->fd, rw->rw_data, rw->rw_size);
    }
    else
    {
	memcpy(rw->rw_data, vaddr, rw->rw_size);
	return rw->rw_size;
    }
}

/*
 *	Write memory.
 */
int
write_mem(rw_t* rw)
{
    void		*vaddr;
    COREINFO		*cp;
    unsigned int	boardnum = rw->rw_module;

    if (boardnum >= NumCore)
	return 0;
    cp = &CoreInfo[boardnum];
    vaddr = map_range(cp, rw->rw_addr, rw->rw_size);
    if (vaddr == (void *) -1)
    {
	/* Not an mmap device, or need to fallback to lseek/write */
	if (cp->fd == -1)
	    return 0;
	lseek(cp->fd, (off_t) rw->rw_addr + cp->mmap_addr, 0);
	return write(cp->fd, rw->rw_data, rw->rw_size);
    }
    else
    {
	memcpy(vaddr, rw->rw_data, rw->rw_size);
	return rw->rw_size;
    }
}

/*
 *	swap routines
 */

static ushort
swapshort(ushort value)
{
    value &= 0xffff;
    return ((value << 8) | (value >> 8));
}

static ulong
swaplong (ulong val)
{
    const ulong mask = 0x00ff00ff;

    ulong d = (val << 16) | (val >> 16);
    return ((d >> 8) & mask) | ((d & mask) << 8);
}


/*
 *	This is the request/response processing loop
 */

void
process(int fd)
{
    int len;
    int rc;
    rw_t rw;
    struct sockaddr client_addr;
    size_t client_len;
    int swap;

    for (;;)
    {
	/*
	 *	Get the next request from the client
	 */

	client_len = sizeof(client_addr);
	len = recvfrom(fd, (void*) &rw, sizeof(rw), 0,
		       (struct sockaddr *) &client_addr, &client_len);
	if (len < 0)
	{
	    perror("recvfrom");
	    exit(5);
	}

	if (Debug)
	    fprintf(stderr, "%s %d bytes at %lx from board=%d module=%d\n",
		   rw.rw_req == RW_READ ? "Read" : "Write",
		   rw.rw_size, rw.rw_addr, rw.rw_board, rw.rw_module);

	/*
	 * 	Convert request to host byte order (hopefully)
	 */

	switch (RwEndianness)
	{
	case RW_ASIS:
	    break;
	case RW_IN_NETWORK_ORDER:
	    rw.rw_addr = ntohl(rw.rw_addr);
	    rw.rw_size = ntohs(rw.rw_size);
	    break;
	default:
	case RW_DETECT:
	    if (rw.rw_size & 0xff00)
	    {
		rw.rw_size = swapshort(rw.rw_size);
		rw.rw_addr = swaplong(rw.rw_addr);
		swap = 1;
	    }
	    else
	    {
		swap = 0;
	    }
	    break;
	}

	/*
	 *	Process the request
	 */

	if (rw.rw_size > sizeof(rw.rw_data))
	    rw.rw_size = sizeof(rw.rw_data);

	switch (rw.rw_req)
	{
	case RW_READ:
	    rw.rw_size = read_mem(&rw);
	    break;

	case RW_WRITE:
	    rw.rw_size = write_mem(&rw);
	    break;
	default:
	    continue;
	}

	if (rw.rw_size > sizeof(rw.rw_data))
	    rw.rw_size = 0;

	/*
	 * 	Convert request back to proper byte order (hopefully)
	 */

	switch (RwEndianness)
	{
	case RW_ASIS:
	    break;
	case RW_IN_NETWORK_ORDER:
	    rw.rw_addr = ntohl(rw.rw_addr);
	    rw.rw_size = ntohs(rw.rw_size);
	    break;
	default:
	case RW_DETECT:
	    if (swap)
	    {
		rw.rw_size = swapshort(rw.rw_size);
		rw.rw_addr = swaplong(rw.rw_addr);
	    }
	    break;
	}

	/*
	 *	Send the reply
	 */

	rc = sendto(fd, &rw, len, 0,
		    (struct sockaddr *) &client_addr, client_len);
	if (rc != len)
	{
	    perror("sendto");
	    exit(6);
	}
    }
}

/*
 *	The usual usage message
 */

void
usage(void)
{
    fprintf(stderr,
    "Usage:	kmed [options]\n"
    "\n"
    "	A server to enable KME access over the network.\n"
    "\n"
    "Options:\n"
    "	-c corefile    Colon-separated list of filename(s) [%s]\n"
    "	                 normal access is via lseek() and read()/write()\n"
    "	                 to use a fixed mmap say: device,offset,size\n"
    "	                 to use a floating mmap say: device,1,size\n"
    "	-e endian      Endianness of RW protocol\n"
    "	                 (0=as-is, 1=network, 2==autodetect) [%d]\n"
    "	-r             Open corefile(s) read-only\n"
    "	-U port        KME access port number. [%d]\n"
    "	-D debug       Debug level. [%d]\n"
    "\n"
    "Standard /etc/services entry:\n"
    "	kme             %d/udp        kme            # kme server\n"
    "\n"
    "Example using two corefiles (2nd uses floating mmap):\n"
    "	kmed -c /dev/kmem:/dev/mem,1,4096\n"
	    , MemName, RwEndianness, UDP_PORT, Debug, UDP_PORT
    );
    exit(1);
}

/*
 *	The main program
 */

int
main(int argc, char *argv[])
{
    struct sockaddr_in serv_addr;
    char ch;
    int sockfd;
    int rc;
    int c;
    extern char *optarg;
    int port = 0;

    /*
     *	Process options
     */
    while ((c = getopt(argc, argv, "c:d:e:p:rU:D:")) != EOF)
    {
	switch (c)
	{
	case 'c':
	case 'd':
	    MemName = optarg; break;
	    break;

	case 'e':
	    RwEndianness = atoi(optarg);
	    break;

	case 'r':
	    ReadOnly = 1;
	    break;

	case 'p':
	case 'U':
	    if (sscanf(optarg, "%d%c", &port, &ch) != 1 || port < 10)
		usage();
	    break;

	case 'D':
	    Debug = atoi(optarg);
	    break;

	default:
	    usage();
	    break;
	}
    }

    coreinfo(MemName);

    /*
     *	Get a socket
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
	perror("Can't open socket");
	exit(2);
    }

    /*
     *	Figure out port number we want to use
     */
    if (port == 0)
    {
	/*
	 *	Get port number from /etc/services entry:
	 *
	 *	kme             2773/udp        kme             # kme
	 */
	struct servent *sep;

	sep = getservbyname("kme", "udp");

	if (sep != 0)
	    port = sep->s_port;
	else
	    port = htons(UDP_PORT);
    }
    else
    {
	port = htons(port) ;
    }

    /*
     *	Fill in INET address structure
     */
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = port;

    /*
     *	Bind to the socket
     */
    rc = bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    if (rc < 0)
    {
	perror("bind failed");
	exit(4);
    }

    /*
     *	Now run the actual server
     */
    process(sockfd);

    exit(0);
}
