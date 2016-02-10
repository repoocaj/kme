/*
 *          Kernel Memory Editor (KME)
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

char kme_c_version[] = "@(#)kme.c $Revision: 1.30 $ $Date: 2005/01/12 04:57:07 $";

#include <sys/types.h>

#include <setjmp.h>
#include <ctype.h>
#include <signal.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include "config.h"
#include "elfcore.h"
#include "signalname.h"

#if HAVE_SOCKET
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netdb.h>
#  include <arpa/inet.h>
#endif

#if COFF && NLIST
#error You are hosed.  Defined either COFF or NLIST - not both.
#endif

/* 
This really should be autoconfiscated, but now that every useful 
system on the planet has a perfectly lovely nlist, I'm not convinced
it's useful.
#if COFF 
# include <filehdr.h>
# include <ldfcn.h>
# include <syms.h> 
#endif */

#if HAVE_GETOPT_H
#  include <getopt.h>
#endif

#if HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif

#if HAVE_SYS_KSYM_H
#  include <sys/ksym.h>
#endif

#if HAVE_ELF_H
#  include <elf.h>
#endif

#if HAVE_NLIST_H
#  include <nlist.h>
#endif

#if HAVE_LIBELF_NLIST_H
#  include <libelf/nlist.h>
#endif

#if HAVE_MMAP 
#  include <sys/mman.h>
#endif

#if HAVE_TERMIOS_H
#   include <termios.h>
#endif

#if HAVE_LIBNCURSES 
#  include <ncurses.h>
#endif

#if HAVE_LIBCURSES 
#  include <curses.h>
#endif

#if defined(KEY_HOME)
#   define TERMINFO 1
#endif

#if HAVE_SYS_PTRACE_H
#  include <sys/ptrace.h>
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

#if HAVE_WAIT_H
#  include <wait.h>
#endif

#if HAVE_SYS_WAIT_H
#  include <sys/wait.h>
#endif

#if HAVE_SOCKET
#   include <netinet/in.h>
#   include <netdb.h>
#endif

#if HAVE_LIBDL
#	include <dlfcn.h>
#endif

#if HAVE_INTTYPES_H
# 	include <inttypes.h>
#endif

/*
 * Now try to set up internal typedefs for things that *have* to be
 * a fixed size.  Use the C99 types if we can.
 */
#if HAVE_INTTYPES_H
typedef uint8_t kme_uint8_t ;
typedef uint16_t kme_uint16_t;
typedef uint32_t kme_uint32_t;
typedef uint64_t kme_uint64_t;
#else
typedef unsigned char kme_uint8_t ;
typedef unsigned short kme_uint16_t;
typedef unsigned long kme_uint32_t;
  #if CC_HAS_LONG_LONG
   typedef unsigned long long kme_uint64_t;
  #endif
#endif

/*
 * The traditional column width for KME is 8 characters, so that
 * there can be eight fields across across an 80 character tty.
 * However a column width of 9 works a lot better to display
 * 32-bit hex data.  Hence the column width is now both a
 * compile time and parameter adjustable variable.
 *  "you can have it your way."
 */

#ifndef COLWIDTH
#  define COLWIDTH 9			/* Column width */
#endif

/* 
 * OLDCURSE indicates a curses where keypad keys (eg arrow keys)
 * do not work when using VMIN=0.  Unfortunately, keypad keys
 * are broken almost everywhere, so OLDCURSE is now default.
 *
 * Updated: 12/11/2004
 *
 * After many years with broken curses (no arrow keys) the
 * modern ncurses libraries now seem to know how to do it (again).
 * So by default OLDCURSE is off, if you have ncurses.h.
 */
#ifndef OLDCURSE
#  if HAVE_LIBNCURSES
#    define OLDCURSE 0
#  else
#    define OLDCURSE 1
#  endif
#endif

/*
 *  Unsigned variables.  This form makes them impervious
 *  to similar defs in sys/types.h.
 */

#define uchar unsigned char
#define ulong unsigned long

/* 
 * This is the (currently incorrect) type of all target addresses
 * used internally.
 */
typedef ulong kme_addr_t; 

#if HAVE_SOCKET
#if HAVE_STROPTS_H
#   include <stropts.h>
#endif	/* HAVE_STROPTS_H */
#endif

extern int optind;
extern char *optarg;

int insert_mode(void);
int update_line(int);
void dopause(const char *msg);

#include "kme.h"
char kme_h_version[] = KME_H_VERSION;

#if OLDCURSE
#define chtype int
#endif

#define ctrl(x) ((x) & 0x1f)

/*
 *  Default file names.
 */

char *corename = "/dev/kmem";
char defsname_default[] = "kme_defs";
char *defsname = defsname_default;
char *symname = 0;
char defspath_default[] = ".:/usr/share/kme";
char *defspath = defspath_default;

/*
 *  Interface to SC vi-edit routines.
 */

#define LINELEN	200			/* Max line size */

extern int linepos;			/* Line position */
extern char line[];			/* Line buffer */
extern int (*lineproc)();		/* Line processing procedure */

extern int mode_ind;			/* Mode indicator */

/*
 *  Address field definition.
 */

typedef struct
{
    long    a_offset;			/* Display offset */
    long    a_iaddr;			/* Indirect address */
    long    a_eaddr;			/* Effective address */
    char*   a_disp;			/* Display string */
    int	    a_size;			/* Number of bytes */
    int	    a_grow;			/* Automatically unobscure itself */
} a_field;

kme_addr_t addr_mask  = -1UL;		/* All addresses read from a symbol
					   file are masked with this */

#define NADR	400			/* Max number of addresses */
a_field afield[NADR];			/* Address fields */
a_field bfield[NADR];			/* Address buffer */

/*
 *  Data field definition.
 */

typedef struct
{
    kme_addr_t   d_addr;		/* Address */
    int	    d_type;			/* Type */
} d_field;

#define NDATA	(200 * 12)		/* Max number of data columns */

d_field dfield[NDATA];			/* Column fields */

#define DFIELD(r,c) (dfield[ncol * ((r) - frow) + (c) - 1])

/*
 *  Field row/column positioning.
 */

#define FROW(r) ((r) - frow)
/*
 * If we're on a system with 64-bit addresses, if a screen is bigger
 * than this, we display all the address bits.
 */
#define BIG_SCREEN 80 
/*
 * If screen space is plentiful and our addresses may be longer than 
 * 8 bytes long, we bump the first colum over to make space for them.
 */
#define START_DATA_COL (((sizeof(kme_addr_t) > 4) && COLS > BIG_SCREEN) ? 15 : 7)
#define FCOL(c) ((c) ? (colwidth * (c) + START_DATA_COL) : 5)

int initial_display_row;		/* To track order of automatic
					   insertions */

/*
 *  Terminal structure.
 */
#if HAVE_TERMIOS_H
  struct termios otio;			/* Original terminal structure */
  struct termios tio;			/* Curses terminal structure */
#else /* HAVE_TERMIOS_H */
  struct termio otio;			/* Original terminal structure */
  struct termio tio;			/* Curses terminal structure */
#endif /* HAVE_TERMIOS_H */

/*
 *  Expression parsing variables.
 */

jmp_buf efail;				/* Error recovery */
char *str;				/* String position */
int inparen = 0;			/* In parenthesis */
int ebase;				/* Default input base */

/*
 *  Symbol structure.
 */

typedef struct symbol SYM;

struct symbol
{
    SYM*    s_next;			/* Next item on list */
    ulong   s_value;			/* Symbol value */
    char    s_name[10];			/* Symbol name */
};

#define NHASH	2909			/* Hash table size (prime) */

SYM* symbol[NHASH];			/* Hash table */

char *symparam;				/* Symbol file name parameter */

/*
 *  Temp variables.
 */

#define TEMP_MAX	10

ulong temp[TEMP_MAX];			/* Temp store variables */
int width = 4;

/*
 *  Miscellaneous flags.
 */

char *progname;				/* Program name */

int sigint;				/* Got an interrupt */
int sigalarm;				/* Got an alarm */

struct sigaction sigaction_int;		/* Signal actions for int */

#if HAVE_PTRACE
struct sigaction sigaction_alarm;	/* Signal actions for alarm */
struct sigaction sigaction_child;	/* Signal actions for child */
#endif

int idline;				/* Call idlok() */

int quitreq;				/* Exit quick */
int debug;				/* Debug on */

int depth;				/* Recursion depth */

int scount;				/* Scroll count */

int colwidth = COLWIDTH;		/* Column width */
int uptime = 2;				/* Screen update time */
int helpflag = 0;			/* Show help information */
int addrflag = 1;			/* Show addresses */
int writeflag = 1;			/* Open core file for write */

int swapflag = 0;			/* Swap "endianness" for
					   words & longs */

int index_mode = 1;			/* display array as
					   "foo+0x80" or "foo[1]" */

int addrscale = 1;			/* Scale displayed addresses */

int rw_max = 128;			/* Max readahead */

int requested_kid_pause;
int kid_signal;				/* Watch for this signal in the child,
					 * refresh our display and pause it,
					 * but immediately restart the child.
					 */

/*
 *  Special FEP support variables.
 */

int def_board;				/* Default board number */
int def_module;				/* Default concentrator number */
int ov_board;				/* Overide board number */
int ov_module;				/* Overide concentrator number */

#if HAVE_SOCKET
int nsoc;				/* Actual number of sockets */
char *fepdev;				/* FEP device name */
#endif

#if HAVE_STROPTS_H
int strdev = 0;
#endif

#if HAVE_SOCKET
#define RW_PAKLEN \
((unsigned)&((rw_t *)0)->rw_data[sizeof(((rw_t *)0)->rw_data)])

#define MAXSOC 10			/* Max number of open sockets */
int socfd[MAXSOC];			/* Socket file descriptor */

char *hostname;				/* Host name list */
int udp_port;				/* UDP port number */
char *gdb_port = "9000";		/* GDB port number */
#endif

int use_gdb = 0;			/* Using GDB protocol over sockets */

int fepfd;				/* FEP file descriptor */
char *elfcore;				/* Filename of corefile to read. */

/*
 *  Screen variables.
 */

int onscreen;				/* This row on the screen */

int brow;				/* Number of rows in buffer */

int frow;				/* First row on the screen */
int lrow;				/* First row on last refresh */
int trow;				/* Temporary version of frow */

int crow;				/* Current display row */
int erow;				/* End of display area */

int irow;				/* Input row */
int icol;				/* Input column */

int orow;				/* Output row */
int ocol;				/* Output column */

int nrow;				/* Number of rows on screen */
int ncol;				/* Number of columns on screen */

int indirect_row;			/* Indirect operator row */

/*
 *  Formatting strings.
 */

char *format[26];			/* Display formatting strings */
char keyformat[26];			/* Format entered from
					   command/keyboard */

/*
 * Symbolic formatting strings.
 */

typedef struct
{
    char * name;			/* Name entered from command or kbd */
    char * format;			/* Display formatting strings */ 
    char *(*dlfunc)(kme_addr_t *addrp, int dlarg);	/* Function */
    int   dlarg;			/* User defined function argument */
} d_fmt;

int fmt_cnt;
int dl_fmt_cnt;

#define NSYM_FORMATS	10000		/* Max number of symbol formattings */

d_fmt dfmt[NSYM_FORMATS];


int readch(void);

char hexnum[] = "0123456789abcdefghijklmnopqrstuvwxyz";

/*
 *  Core memory access.
 */

char *coredev;				/* Core file parameter */

kme_addr_t base;			/* Base address */
kme_addr_t addr;			/* Memory address being displayed */
kme_addr_t faddr;			/* First address this display item */

int lboard;				/* Last board number */
int lconc;				/* Last conc number */
kme_addr_t laddr;			/* Low read address */
kme_addr_t haddr;			/* High read address */

int memfd;				/* Core file descriptor */

long mem[128];				/* Memory array */

#if HAVE_MMAP
kme_addr_t	mempa;			/* Memory phys addr */
kme_addr_t	memlen;			/* Memory length */
uchar	*memva;				/* Virtual address */
#endif

#define	ALIGNED(ADDR,BYTES)	(! (((long) ADDR) & (BYTES-1)))

#if HAVE_PTRACE
int pid;				/* Ptrace process id */
int pidrun;				/* Process is running */
#endif

/*
 *  Help strings.
 */

char *helptext[] =
{
    "+/- (c)hange (d)elete (e)dit (i)nsert (m)acro (p)ut (y)ank (q)uit (z)ero",
    "Enter address/format",
    "Enter the value to store in memory",
};


/************************************************************************
 *  catch - catch interrupts gracefully
 ************************************************************************/

void
catch_int(int sig)
{
    sigint = 1;

    beep();
}


/************************************************************************
 *  catch_alarm - catch alarms gracefully.
 ************************************************************************/

void
catch_alarm(int sig)
{
    sigalarm = 1;
}


/************************************************************************
 *  handle_kid_status - Handles the wait(&status) returned from
 *                      a PTRACEd child process.
 ************************************************************************/

#if HAVE_PTRACE
void
handle_kid_status(int status)
{
    if (WIFSTOPPED(status))
    {
	int ksig = WSTOPSIG(status);
	/* 
	 * If target is paused by the signal specified by -I, just do
	 * the restart immediately after noting this so we can run one
	 * screen refresh sycle.
	 */
	if (ksig == kid_signal) {
		requested_kid_pause = TRUE;
	}
	if (ptrace(PTRACE_CONT, pid, 0, ksig) != 0)
	    quitreq = 2;
    }
    else
    {
	quitreq = 2;
    }
}
#endif

/************************************************************************
 *  watch_the_kid - Catches PTRACE child interrupts and gets the
 *                  kid back to work.
 ************************************************************************/

#if HAVE_PTRACE
void
watch_the_kid(int sig)
{
    int kid;
    int status;

    kid = wait(&status);

    if (kid != pid)
    {
	fprintf(stderr, "Whazza %d != %d\n", kid, pid);
	return;
    }

    handle_kid_status(status);
}
#endif

/************************************************************************
 *  bigendian - Return true if we are on a bigendian host
 ************************************************************************/

int bigendian()
{
    int	endian = 1;

    return ((char *) &endian)[0] != 1;
}


/************************************************************************
 *  swaps - Swap short operand.
 ************************************************************************/

kme_uint16_t 
always_swaps(kme_uint16_t mval)
{
    mval = (((mval >> 8) & 0xff) | ((mval << 8) & 0xff00));
    return (mval);
}


kme_uint16_t
swaps(kme_uint16_t mval)
{
    if (swapflag)
    {
	mval = always_swaps(mval);
    }
    
    return (mval);
}


/************************************************************************
 *  swapl - Swap long operand.
 ************************************************************************/

kme_uint32_t 
always_swapl(kme_uint32_t mval)
{
    mval = (((mval >> 24) & 0x000000ff) |
	    ((mval >>  8) & 0x0000ff00) |
	    ((mval <<  8) & 0x00ff0000) |
	    ((mval << 24) & 0xff000000));
    return(mval);
}


kme_uint32_t
swapl(kme_uint32_t mval)
{
    if (swapflag)
	mval = always_swapl(mval);
    
    return (mval);
}

#if CC_HAS_LONG_LONG
/************************************************************************
 *  swapq - Swap quadword operand.
 ************************************************************************/

kme_uint64_t 
always_swapq(kme_uint64_t mval)
{

    mval = ( ((mval & 0x00000000000000ffULL) << 56) |      
	     ((mval & 0x000000000000ff00ULL) << 40) |      
	     ((mval & 0x0000000000ff0000ULL) << 24) |      
	     ((mval & 0x00000000ff000000ULL) <<  8) |      
	     ((mval & 0x000000ff00000000ULL) >>  8) |      
	     ((mval & 0x0000ff0000000000ULL) >> 24) |      
	     ((mval & 0x00ff000000000000ULL) >> 40) |      
	     ((mval & 0xff00000000000000ULL) >> 56)
	    );

    return(mval);
}


kme_uint64_t
swapq(kme_uint64_t mval)
{
    if (swapflag)
	mval = always_swapq(mval);
    
    return (mval);
}
#endif /* CC_HAS_LONG_LONG */

/************************************************************************
 *  stralloc - Allocate string.
 ************************************************************************/

char *
stralloc(char* s)
{
    char *d;

    if (s == 0) return(0);

    d = (char *)malloc((unsigned)strlen(s)+1);
    if (d) strcpy(d, s);

    return(d);
}



/************************************************************************
 *  strfree - Free string.
 ************************************************************************/

void 
strfree(char* s)
{
    if (s) free((void *)s);
}


/************************************************************************
 *  Find a display symbol name in the tables
 *  Returns index in array.
 *
 *  FIXME: Optimization Opportunity.  Rather
 *  than ripping through these sequentially,
 *  do a hashed lookup.  This has to consume
 *  killer clock cycles, and it's run on each
 *  screen update.....
 * 
 ************************************************************************/

int 
find_format(char* s)
{
    int x;
    for (x=0;x<fmt_cnt;x++)
	if (strcmp(dfmt[x].name,s) == 0)
	    return (x);
    return(-1);

}


/************************************************************************
 * Add a symbolic format to the table.
 * FIXME: should build hash table (index?) hinted at above...
 ************************************************************************/

void
add_format(char *name, char *format,
	char * (*dlfunc)(kme_addr_t *addrp, int dlarg), int dlarg)
{
    dfmt[fmt_cnt].name = stralloc(name);
    dfmt[fmt_cnt].format = format ? stralloc(format) : NULL;
    dfmt[fmt_cnt].dlfunc = dlfunc;
    dfmt[fmt_cnt].dlarg = dlarg;

    if (debug)
	fprintf(stderr,
		"Defined full command: (%s)%d = %s\n",
		dfmt[fmt_cnt].name, fmt_cnt,
		format ? dfmt[fmt_cnt].format : "function");

    if (fmt_cnt++ > NSYM_FORMATS)
    {
	fprintf(stderr, "\nFATAL: exceeded %d symbolic formats\n", 
		NSYM_FORMATS);
	exit(1);
    }
}
	

/************************************************************************
 *  getbase - Convert the next few characters of an ascii
 *            string to a number of the specified base.
 ************************************************************************/

char *
getbase(char* s, ulong* value, int b)
{
    ulong v;
    int n;
    int d;

    /* Handle float */

    if (b == -4)
    {
	float f;
	char *ss = s;

	while ('0' <= *s && *s <= '9')
	    s++;

	if (*s == '.')
	{
	    s++;
	    while ('0' <= *s && *s <= '9')
	        s++;
	}

	if (s == ss)
	    return 0;

	if (*s == 'e')
	{
	    s++;
	    if (*s == '-' || *s == '+')
		s++;
	    while ('0' <= *s && *s <= '9')
	        s++;
	}

	if (sscanf(ss, "%f", &f) != 1)
	    return 0;

	*value = *(ulong*)&f;
	return s;
    }

    /* Handle integer */

    if (s[0] == '0')
    {
	if (s[1] == 't') { b = 10;s += 2;}
	else if (s[1] == 'x') { b = 16;s += 2;}
	else b = 16;
    }
    
    v = 0;
    for (n = 0;; n++)
    {
	if ('0' <= *s && *s <= '9') d = *s - '0';
	else if ('a' <= *s && *s <= 'z') d = *s - 'a' + 10;
	else if ('A' <= *s && *s <= 'z') d = *s - 'A' + 10;
	else break;

	if (d >= b) break;

	v = b * v + d;
	s++;
    }
    
    if (n == 0) return(0);
    
    *value = v;
    
    return(s);
}


/************************************************************************
 *  getdigit - Convert an alphanumeric digit to a number.
 ************************************************************************/

int 
getdigit(int c)
{
    return(c <= '9' ? c - '0' : c < 'Z' ? c - 'A' + 10 : c - 'a' + 10);
}


/************************************************************************
 *  readdefs - Read formatting strings from a file.
 ************************************************************************/

int readdefs()
{
    register char *cp;
    register int ch;
    register int i;
    register int nstring;
    register int cmd;
    int full_cmd;
    int rtn;
    int quote;
    int bang;
    char *s;
    char *f;
    FILE *file;
    char buf[200];
    char string[32768];
    char fmt_name[200];
    char *fmt_name_index;

    /*
     *	Discard old format strings.
     */

    for (i = 0;i < 26;i++)
    {
	if (!keyformat[i])
	{
	    strfree(format[i]);
	    format[i] = 0;
	}
    }

    fmt_cnt = dl_fmt_cnt;

    /*
     *	Loop to open several filenames separated
     *	by colons.
     */

    rtn = 1;

    for (f = defsname;*f;)
    {
	/*
	 *  Open the file.
	 */

	s = buf;
	while (*f && (ch = *f++) != ':') *s++ = ch;
	*s = 0;

	if (*buf == '/')
	{
	    file = fopen(buf, "r");
	    if (file == 0)
	    {
		rtn = 0;
		fprintf(stderr, "Warning: could not open ");
		perror(buf);
		continue;
	    }
	}
	else
	{
	    char path[200];
	    char fullpath[200];
	    char *p, *ps;

	    file = 0;
	    for (p = defspath; *p;)
	    {
		ps = path;
		while (*p && (ch = *p++) != ':') *ps++ = ch;
		*ps = 0;
		if (*path == 0) strcpy(path, ".");

		sprintf(fullpath, "%s/%s", path, buf);
		file = fopen(fullpath, "r");
		if (file)
		    break;
	    }
	    if (file == 0)
	    {
		if (strcmp(buf, "kme_defs"))
		{
		    rtn = 0;
		    fprintf(stderr, "Warning: could not find ");
		    perror(buf);
		}
		continue;
	    }
	}

	/*
	 *  Read lines from the file.
	 */

	cmd = 0;
	full_cmd = 0;
	nstring = 0;

	for (;;)
	{
	    /*
	     *	Get next line from input.
	     */

	    s = fgets(buf, sizeof(buf), file);

	    /*
	     *	If EOF, or a new command, allocate space for
	     *	the last command.
	     */

	    if (s == 0 || isupper(buf[0]) || (buf[0] == '!'))
	    {
		if (cmd && !keyformat[cmd -= 'A'])
		{
		    strfree(format[cmd]);

		    string[nstring] = 0;
		    format[cmd] = stralloc(string);

		    if (debug)
		    {
			fprintf(stderr, "Defined command: %c = %s\n",
				cmd + 'A', string);
		    }
		}
		else if (full_cmd)
		{
		    string[nstring] = 0;
		    add_format(fmt_name, string, NULL, 0);
		}
		cmd = 0;
		full_cmd = 0;
		nstring = 0;
	    }

	    /*
	     *	Exit on EOF.
	     */

	    if (s == 0) break;

	    /*
	     *	A line beginning with an upper case letter begins
	     *	a definition.  A line who's first non-white space
	     *  is a "#" is a comment.  Other lines are continuations.
	     *	Single and double quoted strings are passed verbatim,
	     *	otherwise white space is discarded.
	     *  Behaviour Change 07/02/94 robertl@arnet.com.....
	     *  If the first character is a '!', we take that as
	     *  a "symbolic" format name.  
	     *  Symbolic format names are not recursive.
	     *  Symbolic names may contain any printing character.
	     */

	    cp = &buf[0];

	    /*
	     * Check for a new format definition name at beginning of line
	     */
	    if (isupper(*cp)) 
	    {
		/*
		 * Its an old style definition
		 */
		cmd = *cp++; 
	    }
	    else if (*cp == '!')
	    {
		/*
		 * Its a new style definition
		 */
		++cp;

		fmt_name_index = fmt_name;
		while (cp && (isalnum(*cp) || (*cp == '_')))
		    *fmt_name_index++ = *cp++;
		*fmt_name_index = 0;

		full_cmd = 1;
	    }

	    /*
	     * Process rest of line
	     */

	    while (isspace(*cp)) cp++;

	    if ((*cp == '#')) continue;

	    quote = 0;
	    bang = 0;

	    while ((ch = *cp++) != 0) 
	    {
		if (quote)
		{
		    if (ch == quote) quote = 0;
		}
		else
		{
		    if (bang)
		    {
			if (!isalnum(ch) && ch != '_') bang = 0;
			if (isspace(ch)) ch = ' ';
		    }
		    else if (isspace(ch)) continue;
		    
		    if (ch == '\'' || ch == '"') quote = ch;
		    else if (ch == '!') bang = 1;
		}

		if (nstring < sizeof(string)-2) string[nstring++] = ch;
	    }

	    if (quote && nstring < sizeof(string)-2) string[nstring++] = ch;
	}

	fclose(file);
    }

    return(rtn);
}


/************************************************************************
 *  hashname - Hash name to symbol table pointer.
 ************************************************************************/

SYM**
hashname(char* name)
{
    register ulong v;

    v = 0;

    while (*name)
    {
	v += *name++;
	v *= 23;
    }

    return symbol + (v % NHASH);
}


/************************************************************************
 *  readsym - Read namelist file.
 ************************************************************************/

int
readsym()
{
    register SYM **spp;
    register SYM *sp;
    register char *cp;
    char *f;
    int ch;
    int rtn;
    FILE *file;
    ulong value;
    char *name;
    char buf[512];
    unsigned n;

#if COFF
    LDFILE  *ldp;
    SYMENT  sym;
    int i;
    unsigned short  magic;
#endif

    /*
     *  Clear the current namelist file.
     */

    for (spp = symbol;spp < symbol + NHASH;spp++)
    {
        while ((sp = *spp) != 0)
        {
            *spp = sp->s_next;
            free((void *)sp);
        }
    }

    /*
     *  Read in the new symbol file.
     */

    rtn = 1;

    for (f = symname;*f;)
    {
        /*
         *  Open the file.
         */

        cp = buf;
        while (*f && (ch = *f++) != ':') *cp++ = ch;
        *cp = 0;

        file = fopen(buf, "r");
        if (file == 0)
        {
            rtn = 0;
            continue;
        }

#if COFF
        /*
         * See if file is COFF
         */

        n = fread(&magic, sizeof(magic), 1, file);
        if (n == 1 && ISCOFF(magic))
        {
            /*
             * COFF namelist
             */

            fclose(file);
            ldp = ldopen(buf, (LDFILE *) NULL);
            if (ldp)
            {
                for (i = 0; ldtbread(ldp, i, &sym) != FAILURE; ++i)
                {
                    i += sym.n_numaux;	/* Skip AUX info */
                    name = ldgetname(ldp, &sym);
                    if (!name) continue;
                    if (sym.n_sclass != C_EXT) continue;
                    spp = hashname(name);
                    n = (unsigned)&((SYM*)0)->s_name[strlen(name)+1];
                    sp = (SYM*)malloc(n);
                    if (sp == 0)
                    {
                        fprintf(stderr, "Out of memory");
                        exit(1);
                    }
                    sp->s_next = *spp;
                    *spp = sp;
                    sp->s_value = sym.n_value & addr_mask;
                    strcpy(sp->s_name, name);
                    if (debug)
                    {
                        fprintf(stderr, "Added Symbol %s = %x\n",
				sp->s_name, sp->s_value);
                    }
                }
		ldclose(ldp);
            }
	    continue;
        }

	fseek(file, 0L, 0);
#endif
#if HAVE_NLIST
	/* 
	   If we're able to use nlist(2), we don't have to 
	   read this whole thing into core.  But to allow
	   the ability for a single kme binary to work with 
	   both native binaries (say, /unix) and binaries 
	   that we provide an ASCII symbol table for (cay, 
	   csfep.sym), let's read some stuff.  If any of it
	   is non-ascii, we'll let the nlister take care of 
	   it.
	*/
	{
	    int file_is_binary = 0;

	    n = fread(&buf, 1, sizeof(buf), file);
	    while(n--)
	    {
		if (!isascii(buf[n]))
		    file_is_binary = 1;
	    }
	    if (file_is_binary)
	    {
		if (debug)
		    fprintf(stderr, "File is binary, continuing\n");
		fclose(file);
		continue;
	    }
	}
#endif

	/*
	 *  Read all the symbols in the (ASCII) file.
	 */

	fseek(file, 0L, 0);	
	while (fgets(buf, sizeof(buf), file) != 0)
	{
	    cp = buf;
	    while (isspace(*cp)) cp++;

	    cp = getbase(cp, &value, 16);
	    if (cp == 0)
	    {
		rtn = 0;
		continue;
	    }

	    while (isspace(*cp)) cp++;

	    /*
	     * Try to also handle linux 3 field /boot/System.map format
	     * 		c03ddee4 b inetaddr_chain
	     */
	    if (isalpha(*cp) && cp[1] == ' ' && isgraph(cp[2]))
		cp += 2;

	    name = cp;
	    while (*cp && !isspace(*cp))
		cp++;
	    *cp = 0;

#if HAVE_PROC_KSYMS
	    /*
	     * Strip module Ids from /dev/ksym files.
	     */

	    if (cp - name > 10 && cp[-10] == '_' && cp[-9] == 'R')
	    {
		char *hp = cp - 8;
		
		while (isxdigit(*hp))
		    hp++;
		
		if (*hp == 0)
		    cp[-10] = 0;
	    }
#endif	    

	    spp = hashname(name);

	    n = (unsigned)&((SYM*)0)->s_name[strlen(name)+1];

	    sp = (SYM*)malloc(n);

	    if (sp == 0)
	    {
		fprintf(stderr, "Out of memory");
		exit(1);
	    }

	    sp->s_next = *spp;
	    *spp = sp;

	    sp->s_value = value & addr_mask;
	    strcpy(sp->s_name, name);

	    if (debug)
	    {
		fprintf(stderr, "Added Symbol %s = %lx\n",
			sp->s_name, sp->s_value);
	    }
	}
    }

    return(rtn);
}


/************************************************************************
 *  stopchild - Stops ptrace() child for memory inspection.
 ************************************************************************/

#if HAVE_PTRACE
int
stopchild()
{
    int status;
    
    if (!pidrun)
	return 1;

    pidrun = 0;

    /* Stop catching his signals */

    signal(SIGCLD, SIG_IGN);

    /* Stop him */

    if (kill(pid, SIGSTOP) != 0)
    {
	perror("SIGSTOP");

	if (errno == ESRCH || 1)
	    quitreq = 2;
	    
	return 0;
    }

    /* Wait for him to stop */

    sigalarm = 0;
    alarm(1);

    while (wait(&status) != pid && sigalarm == 0)
	fprintf(stderr, "wait again\n");
	    
    alarm(0);

    return 1;
}
#endif


/************************************************************************
 *  startchild - Starts ptrace() child when memory inspection complete.
 ************************************************************************/

#if HAVE_PTRACE
int
startchild()
{
    /* Ignore if he is alread running */

    if (pidrun)
	return 0;

    pidrun = 1;

    /* Arrange to catch all his signals (again) */

    sigaction(SIGCLD, &sigaction_child, 0);

    /* Start him up */

    if (ptrace(PTRACE_CONT, pid, 0, 0) != 0)
    {
	perror("PTRACE_CONT");
	quitreq = 2;
	    
	return 0;
    }

    return 1;
}
#endif


/************************************************************************
 *  gdb_cmd/resp - GDB protocol utils
 *
 *  this needs to be beefed up in the face of protocol errors.
 ************************************************************************/

int
key_ready(void)
{
    fd_set fdr;
    struct timeval tv;
    FD_ZERO(&fdr);
    FD_SET(0, &fdr);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    return select(1, &fdr, NULL, NULL, &tv) == 1;
}

int
gdb_rcv1(int fd)
{
    fd_set fdr;
    struct timeval tv;
    unsigned char ch;
    int rlen;

    FD_ZERO(&fdr);
    FD_SET(fd, &fdr);
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    if (select(FD_SETSIZE, &fdr, NULL, NULL, &tv) < 1)
	return (-1);
    rlen = read(fd, &ch, 1);
    if (rlen != 1)
	return (-2);
    if (debug) fprintf(stderr, "GDB rcv1 <%c>\n", ch);
    return (ch);
}

void
gdb_flushin(int fd)
{
    while (gdb_rcv1(fd) >= 0)
	    {}
}

int
gdb_send(int fd, char *cmd)
{
    char buf[512];
    unsigned char *cp;
    int c;
    int sum;
    int wlen, rc;
    int try;

    for (sum = 0, cp = cmd; *cp; sum += *cp++)
	{}
    wlen = sprintf(buf, "$%s#%02x", cmd, sum & 0xff);

    for (try = 0; try < 3; ++try)
    {
	if (debug) fprintf(stderr, "GDB send %d <%s>\n", wlen, buf);

	rc = write(fd, buf, wlen);
	if (rc != wlen)
	    return 1;

	c = gdb_rcv1(fd);
	if (c < 0)
	    continue;
	if (c == '+')
	    return 0;
    }
    return 2;
}

int
gdb_rcv(int fd, char *buf)
{
    int c, c1, c2;
    int sum;
    char *obuf = buf;

    for (;;)
    {
	c = gdb_rcv1(fd);
	if (c < 0)
	    return (-1);
	if (c == '$')
	    break;
    }

    *buf = 0;
    sum = 0;
    for (;;)
    {
	c = gdb_rcv1(fd);
	if (c < 0)
	    return (-2);
	if (c == '#')
	    break;
	*buf++ = c;
	sum += c;
    }
    c1 = gdb_rcv1(fd);
    if (c1 < 0)
	    return (-3);
    c2 = gdb_rcv1(fd);
    if (c2 < 0)
	    return (-4);
    /*
     * Should the check checksum here. TODO.
     */
    if (debug) fprintf(stderr, "GDB send +\n");
    write(fd, "+", 1);
    *buf = 0;
    return (buf - obuf);
}

void
gdb_detach(int fd)
{
    char buf[512];

    gdb_send(fd, "D");
    gdb_rcv(fd, buf);
}

/************************************************************************
 *  getmem - Get memory bytes.
 ************************************************************************/

int
getmem(unsigned nbyte)
{
    int rw_size = rw_max < nbyte ? rw_max : nbyte;
    long a = (addr + base) & addr_mask;

    static rw_t rw;
    static int rtncode;

    /*
     * If the data is already in the read-ahead buffer,
     * there is no need to fetch it.
     */

    if (ov_board == lboard &&
	ov_module == lconc &&
	a >= laddr &&
	a + nbyte <= haddr)
    {
	goto rw_return;
    }

#if HAVE_STROPTS_H
    if (fepdev && strdev)
    {
	struct strioctl stio;

	/*
	 * Build the request and do the ioctl.
	 */

	rw.rw_req = RW_READ;
	rw.rw_size = rw_size;

	rw.rw_board  = lboard = ov_board;
	rw.rw_module = lconc  = ov_module;
	rw.rw_addr   = laddr  = a;

	stio.ic_cmd = DIGI_KME;
	stio.ic_timout = 0;
	stio.ic_len = sizeof(rw);
	stio.ic_dp = (char *) &rw;

	if (ioctl(fepfd, I_STR, &stio) == -1 || rw.rw_size < nbyte)
	{
	    haddr = rw.rw_addr + 1024;
	    rtncode = 0;
	}
	else
	{
	    haddr = rw.rw_addr + rw.rw_size;
	    rtncode = 1;
	}

	goto rw_return;
    }
#endif /* HAVE_STROPTS_H */

    /*
     *	Read memory from the DigiBoard special device
     *	driver hooks.
     */

    if (fepdev)
    {
	/*
	 * Build the request and do the ioctl.
	 */

	rw.rw_req = RW_READ;
	rw.rw_size = rw_size;

	rw.rw_board  = lboard = ov_board;
	rw.rw_module = lconc  = ov_module;
	rw.rw_addr   = laddr  = a;

	if (ioctl(fepfd, DIGI_KME, &rw) == -1 || rw.rw_size < nbyte)
	{
	    haddr = rw.rw_addr + 1024;
	    rtncode = 0;
	}
	else
	{
	    haddr = rw.rw_addr + rw.rw_size;
	    rtncode = 1;
	}

	goto rw_return;
    }

#if HAVE_SOCKET

    /*
     * Read memory using GDB protocol (could be a socket or serial)
     */
    if (nsoc && use_gdb)
    {
	char cmd[64];
	char resp[512];
	int fd = socfd[ov_board];
	int len;
	int val;
	char *mp, *cp;

	sprintf(cmd, "m%lx,%d", a, rw_size);
	gdb_send(fd, cmd);

	len = gdb_rcv(fd, resp);
	if (len < 1)
	    return 0;
	if (len & 1)
	    return 0;
	for (mp = (char *) mem, cp = resp; len; cp += 2, len -=2)
	{
	    if (sscanf(cp, "%2x", &val) != 1)
		return 0;
	    *mp++ = val;
	}
	return 1;
    }

    /*
     *	Read memory from TCP/IP style device.
     */

    if (nsoc)
    {
	int fd;
	int rtry;
	int wtry;
	rw_t w;
	struct timeval tv;

	static fd_set fdn;
	static fd_set fdr;

	if (ov_board >= nsoc)
	    return(0);

	/*
	 *  Build the request packet.
	 */

	w.rw_req = RW_READ;
	w.rw_size = rw_size;

	w.rw_board  = lboard = ov_board;
	w.rw_module = lconc  = ov_module;
	w.rw_addr   = laddr  = a;

	w.rw_size = swaps(w.rw_size);
	w.rw_addr = swapl(w.rw_addr);

	fd = socfd[w.rw_board];

	/*
	 *  Repeatedly request the data with UDP until
	 *  a response is heard from the remote.
	 */

	rtncode = 0;

	for (wtry = 0;!rtncode;wtry++)
	{
	    if (wtry >= 3)
	    {
		haddr = laddr + 1024;
		break;
	    }

	    write(fd, (char *)&w, RW_PAKLEN);

	    /*
	     *  Wait for the response packet.
	     */

	    for (rtry = 0; !rtncode && rtry < 3; rtry++)
	    {
		/*
		 *  On timeout, exit with bad return status.
		 */

		FD_ZERO(&fdr);
		FD_SET(fd, &fdr);

		tv.tv_sec = 1;
		tv.tv_usec = 0;

		if (select(FD_SETSIZE, &fdr, &fdn, &fdn, &tv) < 1)
		    break;

		/*
		 *  On response from the correct file descriptor,
		 *  with the response we expect, accept the response.
		 *
		 *  Note that it is possible to get an irrelevant
		 *  response if on a previous request, the network
		 *  delays exceeded our simplistic 2 second timeout.
		 */

		if (read(fd, (char *)&rw, RW_PAKLEN) == RW_PAKLEN &&
		    rw.rw_req    == w.rw_req &&
		    rw.rw_board  == w.rw_board &&
		    rw.rw_module == w.rw_module &&
		    rw.rw_addr   == w.rw_addr)
		{
		    rw.rw_size = swaps(rw.rw_size);
		    rw.rw_addr = swapl(rw.rw_addr);

		    if (rw.rw_size < nbyte)
		    {
			haddr = rw.rw_addr + 0x100;
			return(0);
		    }

		    haddr = rw.rw_addr + rw.rw_size;
		    rtncode = 1;
		    break;
		}
	    }
	}

	goto rw_return;
    }
#endif

#if HAVE_MMAP
    /*
     * Read data from mapped file.
     */

    if (memlen)
    {
	uchar	*p = memva + a;

	if (a < 0 || a + nbyte > memlen)
	    return(0);

	memcpy((char *) mem, p, (unsigned) nbyte);

	return(1);
    }
#endif

    /*
     * Read data from an ELF corefile.
     */
    if (elfcore) {
	off_t off;
	long n;
	long r;

	n = find_in_corefile(a, nbyte, &off);
	r = getmem_from_corefile(n, a, mem);
	if (r < 0)
		return 0;
	return 1;
    }

#if HAVE_PTRACE
    /*
     * Ptrace memory read.
     */

    if (pid)
    {
	if (!stopchild())
	    return 0;

	int i;
	for (i = 0; i < nbyte; i++)
	{
	    *(int*)((void*)mem+i) = ptrace(PTRACE_PEEKDATA, pid, a+i, 0);
	    if (errno)
		return 0;
	}
	return 1;
    }
#endif	

    /*
     * Plain old memory read.
     */

    if (lseek(memfd, a, SEEK_SET) != a ||
	read(memfd, (char *)mem, (unsigned) nbyte) != nbyte)
    {
	*(int*)mem = errno;
	return 1;
    }

    return 1;

    /*
     * Return data in rw buffer.
     */

 rw_return:
    if (rtncode)
    {
	memcpy((char *)mem,
	       (char *)&rw.rw_data[a-laddr],
	       nbyte);
    }

    return rtncode;
}


/************************************************************************
 *   showhelp - Show help if selected.
 ************************************************************************/

void showhelp(int index)
{
    char *hp;

    if (!helpflag) return;

    hp = helptext[index];

    move(LINES-1, 0);
    clrtoeol();
    move(LINES-1, (COLS - strlen(hp)) / 2);
    printw("%s", hp);
}


/************************************************************************
 *   clearline - Clear the current line.
 ************************************************************************/

void clearline()
{
    register d_field *dp;
    register int i;

    assert(frow <= crow && crow < frow + nrow);

    /*
     *	Show line number.
     */

    if (lrow != frow)
    {
	move(FROW(crow), 0);

	if (crow == indirect_row)
	{
	    standout();
	    printw("%3d", crow + 1);
	    standend();
	    printw("| ");
	    
	} else {

	    standend();
	    printw("%3d| ", crow + 1);
	}
    }

    /*
     *	Clear remainder of line.
     */

    move(FROW(crow), FCOL(0));
    clrtoeol();

    /*
     *	Highlight the selected field.
     */

    if (irow == crow)
    {
	move(FROW(irow), FCOL(icol));
	standout();
	printw("    ");
    }

    /*
     *	Clear data fields this line.
     */

    dp = &DFIELD(crow, 1);
    for (i = ncol;--i >= 0;)
    {
	dp->d_type = 0;
	dp->d_addr = 0;
	dp++;
    }
}


/************************************************************************
 *  insertscreenline - shove everything down 'count' lines 
 *  on the scereen
 ************************************************************************/

void
insertscreenline(int count)
{
    int r;
    a_field *ap;
    if (count == 0) count = 1;

    r = NADR;
    ap = &afield[NADR];
    while (r > irow)
    {
	--ap;
	--r;

	if (r >= NADR-count) strfree(ap[0].a_disp);

	if (r >= irow+count) 
	    ap[0] = ap[-count];
	else
	{
	    ap[0].a_offset = 0;
	    ap[0].a_disp = 0;
	    ap[0].a_size = 0;
	}
    }
}


/************************************************************************
 *  initline - Initialize a line prior to writing on it.
 ************************************************************************/

void initline()
{
    clearline();

    if (irow == crow && icol == 0) standout();
    else standend();

    move(FROW(crow), FCOL(0));
    crow++;

    if (crow < NADR && afield[crow].a_disp) 
    {
	erow = crow;

	/* If it's an automatically growing field, and we are
	 * overlapping, shove everything down.   Try to keep out
	 * cursor in the obvious place.
	 */
	if (afield[crow].a_grow)
	{
	    int orig_irow = irow;
	    irow=crow;
	    insertscreenline(1);
	    irow = orig_irow;
	}
    }
}


/************************************************************************
 *  setup - Setup next field.
 ************************************************************************/

void setup()
{
    /*
     *	At end-of-line, advance to the next line.
     */

    if (ocol == ncol)
    {
	orow++;
	ocol = 0;
    }

    /*
     *	Clear screen rows from the last position data was written
     *  to the current field position.  Write the address of each
     *  line in the first field.  Scale the address displayed by
     *  the scale factor (used when kme'ing things that aren't
     *  byte addressable, like DSPs).
     */

    while (crow <= orow && crow < erow)
    {
	initline();
	
	if ((sizeof(kme_addr_t) > 4) && (COLS <= BIG_SCREEN)) {
		printw("+%08lx", 0xffffffffUL & (addr / addrscale));
	} else {
		printw("%06lx", addr / addrscale);
	}

	afield[orow].a_eaddr = addr;
    }

    /*
     *	Position to the next field, and determine if it is
     *	on the screen.
     */

    ocol++;

    if (frow <= orow && orow < erow)
    {
	onscreen = 1;

	if (irow == orow && icol == ocol) standout();
	else standend();

	move(FROW(orow), FCOL(ocol));
    }
    else
    {
	onscreen = 0;
    }
}


/************************************************************************
 *  printchar - Unambiguously print character.
 ************************************************************************/

void printchar(int ch)
{
    if (ch < 0x20)
	printw("^%c", (ch & 0x1f) + 0x40);
    else if (ch < 0x7f)
	printw(" %c", ch);
    else
	printw("%02x", ch);
}


/************************************************************************
 *  display - Display memory data.
 ************************************************************************/

void display(char* fp)
{
    register int ch;
    register int count;
    register int n;
    register int i;
    ulong a;
    int c;
    static char *endparen;
    int orig_swapflag = swapflag;
    int orig_width = width;
    int have_count;

    count = 0;
    have_count = 0;

    while ((ch = *fp++) != 0)
    {
	/*
	 *  Get count field.
	 */

	if ('0' <= ch && ch <= '9')
	{
	    have_count = 1;
	    count *= 10;
	    count += ch - '0';
	    continue;
	}

	/*
	 *  Process field type.
	 */

	switch (ch)
	{
	    /*
	     *  Skip whitespace.
	     */
	
	case ' ':
	    break;

	    /*
	     *  Process parenthesized expression.
	     */

	case '(':
	    for (;;)
	    {
		ulong a = faddr;
		faddr = addr;
		display(fp);
		faddr = a;
		if (--count <= 0) break;
	    }
	    fp = endparen;
	    break;

	case ')':
	    endparen = fp;
	    return;

	    /*
	     * Negate swapflag.  This allows override on specific display
	     * elements endianness.
	     */

	case '%':
	    swapflag ^= 1;
	    break;

	    /*
	     *  Process string field.
	     */

	case '"':
	case '\'':
	    {
		int hilite = 0;

		setup();

		n = 0;
		while (*fp)
		{
		    /* 
		       FP is in a very wierd format by this time.
		       Many things are already quoted for us.  That's
		       why this is a little funny.
		       FIXME: you should be able to emit a \ in a string,
		       but you can't.  You couldn't do it before I added
		       this either....
		    */
		    if (*fp == '\\')
		    {
			switch (fp[1])
			{

			    /* Implement only a simple toggle, no nesting */
			case 'h': 
			    hilite ^= 1;
			    if (hilite)
				standout();
			    else
				standend(); 
			    fp ++;
			    break;
			}
			fp ++;
			continue;
		    }

		    if (*fp == ch)
		    {
			fp++;
			break;
		    }

		    if (++n > colwidth)
		    {
			setup();
			/* setup may turn off our standout mode... */
			if (hilite)
			    standout();
			n = 1;
		    }

		    if (onscreen)
			addch((chtype) *fp);

		    fp++;
		}
	    }
	    break;

	    /*
	     *  Position to memory relative address.
	     */
	
	case '#':
	    addr = faddr + addrscale * count;
	    break;
	
	    /*
	     *  Save current address in temp variable.
	     */
	
	case '$':
	    if ((unsigned)count < TEMP_MAX) temp[count] = addr;
	    break;

	    /*
	     * Set count from temp variable
	     */

	case '*':
	    if ((unsigned)count >= TEMP_MAX)
		break;
	    count = temp[count];
	    have_count = 1;
	    continue;

	    /*
	     * Save value at current address in a temp variables
	     */

	case '&':
	    if ((unsigned)count >= TEMP_MAX)
		break;

	    if (!getmem(width))
		*(ulong *) mem = 0;

	    switch (width)
	    {
	    case 1: temp[count] = *(kme_uint8_t *) mem; break;
	    case 2: temp[count] = *(kme_uint16_t *) mem; break;
	    case 4: temp[count] = *(kme_uint32_t *) mem; break;
	    }
	    break;

	    /*
	     * Set width parameter (Used by &, ^)
	     */

	case '=':
	    width = count;
	    break;

	    /*
	     * Roundup a value
	     */

	case '^':
	    if ((unsigned)count >= TEMP_MAX)
		break;
	    temp[count] = temp[count] + width - 1;
	    temp[count] /= width;
	    temp[count] *= width;
	    break;

	    /*
	     *  Set readahead max for remainder of this entry.
	     */
	
	case ':':
	    rw_max = ((unsigned)(count - 1) < 128) ? count : 128;
	    break;

	    /*
	     *  Skip memory forward.
	     */

	case '+':
	    if (have_count == 0) count = 1;
	    addr += addrscale * count;
	    break;

	    /*
	     *  Skip memory reverse.
	     */

	case '-':
	    if (have_count == 0) count = 1;
	    addr -= addrscale * count;
	    break;

	    /*
	     *  Blank field.
	     */

	case '.':
	    do {
		setup();
	    } while (--count > 0);
	    break;

	    /*
	     *  Newline.
	     */

	case 'n':
	    orow += count ? count : 1;
	    ocol = 0;
	    break;

	    /*
	     *  String.
	     */

	case 's':
	    do {
		setup();

		n = count <= colwidth ? count : colwidth;

		if (!getmem(n))
		{
		    if (onscreen) printw("????");
		}
		else if (onscreen)
		{
		    for (i = 0;i < n;i++)
		    {
			c = ((uchar *)mem)[i];
			if (c == 0) c = ' ';
			else if (c < ' ' || c >= 0x7f) c = '.';
			addch((chtype) c);
		    }
		}

		count -= n;
		addr += n;
	    } while (count > 0);
	    break;

	    /*
	     *  Process numeric data fields.
	     */

	case 'a':
	case 'b':
	case 'c':
	case 'd':
	case 'e':
	case 'f':
	case 'g':
	case 'h':
	case 'i':
	case 'l':
	case 'q':
	case 't':
	case 'u':
	case 'x':
	case 'w':
	case 'z':
	    if (have_count && count == 0)
		break;
	    for (;;)
	    {
		setup();

		a = addr;

		int srow = orow;
		int scol = ocol;

		switch (ch)
		{

		    /*  ASCII */

		case 'a':
		    if (onscreen)
		    {
			if (!getmem(sizeof(kme_uint8_t)))
			    printw("??");
			else
			{
			    printchar((int)*(kme_uint8_t *)mem & 0x7f);
			}
		    }
		    addr += sizeof(kme_uint8_t);
		    break;

		    /*  Hex byte */

		case 'b':
		    if (onscreen)
		    {
			if (!getmem(sizeof(kme_uint8_t)))
			    printw("??");
			else
			    printw("%02x", *(kme_uint8_t *)mem);
		    }
		    addr += sizeof(kme_uint8_t);
		    break;

		    /*  Character */

		case 'c':
		    if (onscreen)
		    {
			if (!getmem(sizeof(kme_uint8_t)))
			    printw("??");
			else
			{
			    printchar((int)*(kme_uint8_t *)mem);
			}
		    }
		    addr += sizeof(kme_uint8_t);
		    break;

		    /*  Signed decimal word */

		case 'd':
		    if (onscreen)
		    {
			if (!getmem(sizeof(kme_uint16_t)))
			    printw("????");
			else
			    printw("%d", (short) swaps(*(kme_uint16_t *)mem));
		    }
		    addr += sizeof(kme_uint16_t);
		    break;

		    /*  Decimal longword */

		case 'e':
		    if (onscreen)
		    {
			kme_uint32_t mval;
			if (!getmem(sizeof(kme_uint32_t)))
			    printw("????????");
			else
			{
			    mval = swapl(*(kme_uint32_t *)mem);
			    printw("%ld", mval % 10000000);
			    if (mval / 10000000) printw("+");
			}
		    }
		    addr += sizeof(kme_uint32_t);
		    break;

		    /*  Single precision floating point */

		case 'f':
		    if (onscreen)
		    {
			float fval;
			if (!getmem(sizeof(float)))
			{
			    printw("????????");
			}
			else
			{
			    *(unsigned*)&fval = swapl(*(ulong *)&mem);
							  
			    printw("%-*.*g", colwidth, colwidth-6, fval);
			}
		    }
		    addr += sizeof(float);
		    break;

#if CC_HAS_LONG_LONG
		    /* Decimal quadword */

		case 'g':
		    if (onscreen)
		    {
			kme_uint64_t mval;
			char buf[32];

			if (!getmem(sizeof(kme_uint64_t)))
			    sprintf(buf, "????????????????");
			else
			{
			    mval = swapq(*(kme_uint64_t *)mem);
			    sprintf(buf, "%lld", mval);
			    if (strlen(buf) > 2*colwidth)
				strcpy(&buf[2*colwidth-1],"+");
			}

			printw("%.*s", colwidth, buf);
			setup();

			if (strlen(buf) > colwidth)
			    printw("%.*s", colwidth, buf+colwidth);
		    }
		    else
			setup();
		    addr += sizeof(kme_uint64_t);
		    break;
#endif /* CC_HAS_LONG_LONG */

		    /*  Double precision floating point */

		case 'h':
		    if (onscreen)
		    {
			char buf[32];
			
			if (!getmem(sizeof(double)))
			{
			    sprintf(buf, "????????????????");
			}
			else
			{
			    double fval;
			    *(kme_uint64_t*)&fval =
				swapq(*(kme_uint64_t *)mem);

			    sprintf(buf, "%-*.*lg",
				    2*colwidth, 2*colwidth-7, fval);

			    if (strlen(buf) > 2*colwidth)
				strcpy(&buf[2*colwidth-1],"+");
			}

			printw("%.*s", colwidth, buf);
			setup();

			if (strlen(buf) > colwidth)
			    printw("%.*s", colwidth, buf+colwidth);
		    }
		    else
			setup();
		    addr += sizeof(double);
		    break;

		    /*  FEP input buffer */

		case 'i':
		    if (onscreen)
		    {
			if (!getmem(sizeof(kme_uint16_t)))
			{
			    printw("?? ??");
			}
			else
			{
			    printw("%02x ", *((kme_uint8_t *)mem + 1));
			    printchar((int)*(kme_uint8_t *)mem & 0x7f);
			}
		    }
		    addr += sizeof(kme_uint16_t);
		    break;

		    /*  HEX longword */

		case 'l':
		    if (onscreen)
		    {
			kme_uint32_t mval;
			if (!getmem(sizeof(kme_uint32_t)))
			    printw("????????");
			else
			{
			    mval = swapl(*(kme_uint32_t *)mem);
			    if (colwidth <= 8)
				printw("%0*lx", colwidth-1, mval);
			    else
				printw("%08lx", mval);
			}
		    }
		    addr += sizeof(kme_uint32_t);
		    break;

#if CC_HAS_LONG_LONG
		    /* Hex quadword */

		case 'q':
		    if (onscreen)
		    {
			kme_uint64_t mval;
			char buf[32];

			if (!getmem(sizeof(kme_uint64_t)))
			    sprintf(buf, "????????????????");
			else
			{
			    mval = swapq(*(kme_uint64_t *)mem);
			    sprintf(buf, "%016llx", mval);
			}

		        printw("%.*s", colwidth, buf);
			setup();
		        printw("%.*s", colwidth, buf + colwidth);
		    }
		    else
			setup();
		    addr += sizeof(kme_uint64_t);
		    break;
#endif
		    /*  Unsigned decimal byte */

		case 't':
		    if (onscreen)
		    {
			if (!getmem(sizeof(kme_uint8_t)))
			    printw("????");
			else
			    printw("%d", *(kme_uint8_t *)mem);
		    }
		    addr += sizeof(kme_uint8_t);
		    break;

		    /*  Hex word */

		case 'x':
		    if (onscreen)
		    {
			if (!getmem(sizeof(kme_uint16_t)))
			    printw("????");
			else
			    printw("%04x", swaps(*(kme_uint16_t *)mem));
		    }
		    addr += sizeof(kme_uint16_t);
		    break;

		    /*  Unsigned decimal word */

		case 'u':
		    if (onscreen)
		    {
			if (!getmem(sizeof(short)))
			    printw("????");
			else
			{
			    printw("%d", swaps(*(kme_uint16_t *)mem));
			}
		    }
		    addr += sizeof(short);
		    break;

		    /*  Wide formwat single precision floating point */

		case 'w':
		    if (onscreen)
		    {
			char buf[32];
			
			if (!getmem(sizeof(double)))
			{
			    sprintf(buf, "????????????????");
			}
			else
			{
			    float fval;
			    *(kme_uint32_t *)&fval =
				swapl(*(kme_uint32_t *)mem);

			    sprintf(buf, "%-*.8g", 2*colwidth, fval);

			    if (strlen(buf) > 2*colwidth)
				strcpy(&buf[2*colwidth-1],"+");
			}

			printw("%.*s", colwidth, buf);
			setup();

			if (strlen(buf) > colwidth)
			    printw("%.*s", colwidth, buf+colwidth);
		    }
		    else
			setup();
		    addr += sizeof(float);
		    break;

		    /*  Decimal longword */

		case 'z':
		    if (onscreen)
		    {
			if (!getmem(sizeof(kme_uint32_t)))
			    printw("????????");
			else
			    printw("%ld", swapl(*(kme_uint32_t *)mem));
		    }
		    addr += sizeof(kme_uint32_t);
		    break;
		}
		
		if (onscreen)
		{
		    register d_field *dp;

		    dp = &DFIELD(srow, scol);
		    dp->d_type = ch;
		    dp->d_addr = a;
		}

		if (--count <= 0) break;
	    }
	    break;

	    /*
	     * Accept '!' to mean what follows is a symbolic
	     * format name.  Note: These do not recurse.
	     */
	case '!':
	    {
		char fmt_label[200];
		char *flp = fmt_label;
		int x;

		/* Build null terminated fmt_label */
		for (;isalnum(*fp) || (*fp == '_'); *flp++=*fp++); 
		*flp = 0;

		/* 
		 * If this is a legal format, display it,
		 * respect repeat counts.
		 */
		if ((x = find_format(fmt_label)) >= 0)
		{
		    do {
			if (dfmt[x].format)
			    display(dfmt[x].format);
			else
			{
			    char	*fmt;

			    fmt = (*dfmt[x].dlfunc)(&addr, dfmt[x].dlarg);
			    if (fmt)
				display(fmt);
			}
		    } while (--count > 0);

		}
		else if (onscreen)
	        {
		    setup();
		    printw("??Count:%d Format Label \"%s\" unknown", count, fmt_label);
	        }
	    }
	    break;

	    /*
	     *  Handle macro defines & undefined characters.
	     *
	     *  If we hit a recursion depth of 10, assume
	     *  we are in a recursive loop.  Show an error
	     *  backtrace and exit.
	     */

	default:
	    if (isupper(ch) && format[ch - 'A'])
	    {
		depth++;

		if (depth < 10)
		{
		    do {
			display(format[ch - 'A']);
		    } while (--count > 0);
		}

		if (depth >= 10)
		{
		    if (onscreen)
		    {
			setup();
			printw("**** %c", ch);
		    }
		    endparen = "";
		    return;
		}

		depth--;
	    }
	    else if (onscreen)
	    {
		setup();
		printw("?%d%c", count, ch);
	    }
	}

	count = 0;
	have_count = 0;
    }

    swapflag = orig_swapflag;
    width = orig_width;

    endparen = "";
}


/************************************************************************
 *  inbase - Input base address.
 ************************************************************************/

int
inbase(char* buf)
{
    return getbase(buf, &base, 16) != 0;
}


/************************************************************************
 *  getvalue - Get numeric or symbol value when parsing expressions.
 ************************************************************************/

ulong
getname()
{
    register SYM *sp;
    register char *d;
    register char *s;
    char name[100];
    ulong value;

    s = str;

    if (*s < '0' || *s > '9')
    {
	d = name;

	while(d < name + sizeof(name) - 1 &&
	      (isalnum(*s) || *s == '_'))
	    *d++ = *s++;
	
	*d = 0;
	
	if (d != name)
	{
	    sp = *hashname(name);

#if HAVE_GETKSYM
	/* 
	   On UnixWare, give getksym a shot at it before nlist.  
	   This allows symbol detection in dynamically loaded mods.
	   FIXME: If you suspect this code could benefit from greater
 	   commonality with the NLIST case (or even better, a better 
	   abstraction of the symbol table groper) you would be right.
	 */

	    if (!sp)
	    {
		/* 
		   Must not be in the hash table, let's 
		   look it up, then add it .
		*/

		register SYM **spp = hashname(name);
		unsigned long info;
		unsigned long value = 0;
		int n; 

		if (debug)
		   printw("Looking up `%s' ", name);
	 	if (getksym(name, &value, &info))
		   goto try_nlist;

		/* 
		   This mess is blatantly plagarized from elsewhere in 
		   the code.  Sure wish I understood it....
		*/
		n = (unsigned)&((SYM*)0)->s_name[strlen(name)+1];
		if (debug)
		    printw("n is %d\n", n);
		sp = (SYM*)malloc(n);
		if (sp == 0)
		{
		    fprintf(stderr, "Out of memory");
		    exit(1);
		}
		sp->s_next = *spp;
		*spp = sp;
		sp->s_value  = value & addr_mask;

		strcpy(sp->s_name, name);
		if (debug)
		{
		    fprintf(stderr, "Added Symbol %s = %lx\n",
			    sp->s_name, sp->s_value);
		}
		/* 
		   Tell the parser if we found it, by advancing str 
		   and returning the value.
		*/
		str = s;
		return(sp->s_value);
	    }
	    try_nlist:
#endif

#if HAVE_NLIST
	    if (!sp)
	    {
		/* 
		   Must not be in the hash table, let's 
		   look it up, then add it .
		*/

		register SYM **spp = hashname(name);
		int n;
		struct nlist nlist_tbl[2];

		if (debug)
		    printw("Looking up %s\n", name);

		/* 
		   Nlist can (efficiently) look up multiple symbols
		   in one shot.  We don't take advantage of this, but
		   we do attempt to look it up only once....
		   So, nlist_tbl[0] becomes what we're interested in,
		   next comes the null terminator....
		*/
		nlist_tbl[0].n_name  =  name;
		nlist_tbl[1].n_name  =  NULL;
		nlist(symname, nlist_tbl);
		if (!nlist_tbl[0].n_value)
		{
		    /*  Even though it's not a symbol, it could still
		        be a number - probably hex . */
		    goto might_be_a_num;
		}

		/* 
		   This mess is blatantly plagarized from elsewhere in 
		   the code.  Sure wish I understood it....
		*/
		n = (unsigned)&((SYM*)0)->s_name[strlen(name)+1];
		sp = (SYM*)malloc(n);
		if (sp == 0)
		{
		    fprintf(stderr, "Out of memory");
		    exit(1);
		}
		sp->s_next = *spp;
		*spp = sp;
		sp->s_value = nlist_tbl[0].n_value & addr_mask;
		strcpy(sp->s_name, name);
		if (debug)
		{
		    fprintf(stderr, "Added Symbol %s = %lx\n",
			    sp->s_name, sp->s_value);
		}
		/* 
		   Tell the parser if we found it, by advancing str 
		   and returning the value.
		*/
		str = s;
		return(sp->s_value);
	    }
	might_be_a_num:
#endif
	    while (sp)
	    {
		if (strcmp(name, sp->s_name) == 0)
		{
		    str = s;
		    return(sp->s_value);
		}
		sp = sp->s_next;
	    }
	}
    }

    s = getbase(str, &value, ebase);

    if (s == 0 || isalpha(*s) || *s == '_') longjmp(efail, 1);

    str = s;

    return(value);
}


/************************************************************************
 *  expr - Parse expression by recursive descent.
 ************************************************************************/

long
expr(int pri)
{
    long v, v1, v2;

    /*
     *  Handle UNARY prefix operations.
     */

    while (*++str == ' ');

    switch (*str)
    {
    case '-':
	v = -expr(10);
	break;
    
    case '+':
	v = expr(10);
	break;
    
    case '~':
	v = ~expr(10);
	break;
    
    case '*':
	addr = expr(10) & addr_mask;
	if (!getmem(sizeof(kme_uint32_t))) goto fail;
	v = swapl(*(kme_uint32_t *)mem);
	break;
    
    case '@':
	addr = expr(10) & addr_mask;
	if (!getmem(sizeof(kme_uint16_t))) goto fail;
	v = swaps(*(kme_uint16_t *)mem);
	break;
    
    case '#':
	addr = expr(10) & addr_mask;
	if (!getmem(sizeof(kme_uint8_t))) goto fail;
	v = *(kme_uint8_t *)mem;
	break;
    
    case '$':
	v = expr(10);
	if ((unsigned)v >= TEMP_MAX) goto fail;
	v = temp[v];
	break;
    
    case '(':
	inparen++;
	v = expr(0);
	inparen--;
	if (*str != ')') goto fail;
	str++;
	break;

	/*
	 * Unary Override endianness on long.
	 */

    case '%':
	v = always_swapl(expr(10)); 
	break;

	/*
	 * Unary Override endianness on short.
	 */

    case '^':
	v = always_swaps(expr(10)); 
	break;


	/*
	 * Unary extended operators
	 */
    case '<':
	if (0)
	{}
	else if (strncmp(str, "<be32>", 6) == 0)
	{
	    str += 5;
	    v = bigendian() ? expr(10) : always_swapl(expr(10));
	}
	else if (strncmp(str, "<le32>", 6) == 0)
	{
	    str += 5;
	    v = !bigendian() ? expr(10) : always_swapl(expr(10));
	}
	else if (strncmp(str, "<be16>", 6) == 0)
	{
	    str += 5;
	    v = bigendian() ? expr(10) : always_swaps(expr(10));
	}
	else if (strncmp(str, "<le16>", 6) == 0)
	{
	    str += 5;
	    v = !bigendian() ? expr(10) : always_swaps(expr(10));
	}
	else
	    goto fail;
	break;
    
    default:
	v = getname();
    }
    
    /*
     *  Handle BINARY operators.
     */
    
    for (;;)
    {
	switch(*str)
	{
	case ' ':
	    str++;
	    break;

	case '?':
	    if (pri > 1) goto rtn;
	    v1 = expr(0);
	    if (*str != ':') goto fail;
	    v2 = expr(1);
	    v = v ? v1 : v2;
	    break;
	
	case '|':
	    if (pri >= 2) goto rtn;
	    v |= expr(2);
	    break;

	case '^':
	    if (pri >= 3) goto rtn;
	    v ^= expr(3);
	    break;

	case '&':
	    if (pri >= 4) goto rtn;
	    v &= expr(4);
	    break;

	case '!':
	    if (str[1] == '=')
	    {
		str++;
		if (pri >= 5) goto rtn;
		v = v != expr(5);
	    }
	    else
		goto fail;
	    break;

	case '=':
	    if (str[1] == '=')
	    {
		str++;
		if (pri >= 5) goto rtn;
		v = v == expr(5);
	    }
	    else
		goto fail;
	    break;

	case '>':
	    if (str[1] == '>')
	    {
		str++;
		if (pri >= 7) goto rtn;
		v = v >= expr(7);
	    }
	    else if (str[1] == '=')
	    {
		str++;
		if (pri >= 6) goto rtn;
		v = v >= expr(6);
	    }
	    else
	    {
		if (pri >= 6) goto rtn;
		v = v > expr(6);
	    }
	    break;

	case '<':
	    if (str[1] == '<')
	    {
		str++;
		if (pri >= 7) goto rtn;
		v = v << expr(7);
	    }
	    else if (str[1] == '=')
	    {
		str++;
		if (pri >= 6) goto rtn;
		v = v <= expr(6);
	    }
	    else
	    {
		if (pri >= 6) goto rtn;
		v = v < expr(6);
	    }
	    break;

	case '+':
	    if (pri >= 8) goto rtn;
	    v += expr(8);
	    break;

	case '-':
	    if (pri >= 8) goto rtn;
	    v -= expr(8);
	    break;

	case '*':
	    if (pri >= 9) goto rtn;
	    v *= expr(9);
	    break;

	case '/':
	    if (pri >= 9 || !inparen) goto rtn;
	    v /= expr(9);
	    break;

	case '%':
	    if (pri >= 9) goto rtn;
	    v %= expr(9);
	    break;

	case '.':
	    v += expr(10);
	    break;

	case '[':
	    inparen++;
	    v += expr(0);
	    inparen--;
	    if (*str != ']') goto fail;
	    str++;
	    addr = v & addr_mask;
	    if (!getmem(sizeof(kme_uint32_t))) goto fail;
	    v = swapl(*(kme_uint32_t *)mem);
	    break;
	
	case '{':
	    inparen++;
	    v += expr(0);
	    inparen--;
	    if (*str != '}') goto fail;
	    str++;
	    addr = v & addr_mask;
	    if (!getmem(sizeof(kme_uint16_t))) goto fail;
	    v = swaps(*(kme_uint16_t *)mem);
	    break;
	
	case ':':
	case ')':
	case ']':
	case '}':
	    goto rtn;
	
	case 0:
	    goto rtn;
	
	default:
	    goto fail;
	}
    }

 fail:
    longjmp(efail, 1);

 rtn:
    return(v);
}


/************************************************************************
 *  inaddr - Read address string from keyboard.
 ************************************************************************/

int
inaddr(char* buf)
{
    register a_field *ap;

    ap = &afield[irow];

    ap->a_offset = 0;
    ap->a_iaddr = 0;

    /*
     *	An empty field deletes the entry.
     */

    if (*buf == 0)
    {
	strfree(ap->a_disp);
	ap->a_disp = 0;
	return(1);
    }

    strfree(ap->a_disp);

    ap->a_disp = stralloc(buf);

    if (debug)
	fprintf(stderr, "Input %d, %s\n",
		irow+1, ap->a_disp ? ap->a_disp : "null");

    return(1);
}



/************************************************************************
 *  inparam - Input command parameter.
 ************************************************************************/

int
inparam(char* buf)
{
    int cmd;

    /*
     *	Handle define parameter.
     */

    if (isupper(*buf))
    {
	cmd = *buf++ - 'A';
	if (*buf == '=') buf++;
	strfree(format[cmd]);
	format[cmd] = stralloc(buf);
	keyformat[cmd] = 1;

	if (debug) fprintf(stderr,
			   "Defined %c = %s\n", cmd + 'A', buf);
	return(1);
    }

    /*
     *	Handle line=display parameter.
     */

    irow = 0;
    while (isdigit(*buf))
    {
	irow = 10 * irow + *buf++ - '0';
    }

    if (*buf++ != '=') return(0);

    if (irow == 0 && irow < NADR)
    {
	irow = ++initial_display_row;
    	afield[irow].a_grow = 1;
    }

    if (--irow < 0 || irow >= NADR) return(0);


    return(inaddr(buf));
}


/************************************************************************
 *  indata - Input data.
 ************************************************************************/

int
indata(char* buf)
{
    register ulong v;
    register int size;
    register d_field *dp;
    long a;

    /*
     *  Setup call to expression input routine.
     */

    if (setjmp(efail))
	return(0);

    str = buf - 1;
    inparen = 1;

    dp = &DFIELD(irow, icol);

    switch (dp->d_type)
    {
    case 'd' :
    case 'e' :
    case 'g':
    case 't' :
    case 'u' :
    case 'z' :
	ebase = 10;
	break;

    case 'f':
	ebase = -4;
	break;
	
    default:
	ebase = 16;
	break;
    }

    v = expr(0);

    if (*str != 0) return(0);

    switch (dp->d_type)
    {
    case 'a':
    case 'b':
    case 'c':
    case 't':
	size = sizeof(kme_uint8_t);
	*(kme_uint8_t *)mem = v;
	break;

    case 'd':
    case 'i':
    case 'x':
    case 's':
	size = sizeof(kme_uint16_t);
	*(kme_uint16_t *)mem = swaps((kme_uint16_t)v);
	break;

    case 'e':
    case 'l':
    case 'z':
	size = sizeof(kme_uint32_t);
	*(kme_uint32_t *)mem = swapl(v);
	break;

    case 'g':
    case 'q':
	// This won't let you change the upper word until expr() is taught to
	// return 64 bit values.
	size = sizeof(kme_uint64_t);
	*(kme_uint64_t *)mem = swapq(v);
	break;

    case 'f':
    	size = sizeof(float);
	*(ulong *)mem = swapl(v);
	break;

    default:
	return(0);
    }

    a = dp->d_addr + base;

#if HAVE_STROPTS_H
    if (fepdev && strdev)
    {
	static rw_t rw;
	register a_field *ap;
	struct strioctl stio;

	rw.rw_req   = RW_WRITE;
	rw.rw_size  = size;

	for (ap = &afield[irow];ap->a_disp == 0;--ap) {}

	if (isalnum(ap->a_disp[0]) &&
	     isalnum(ap->a_disp[1]) &&
	     ap->a_disp[2] == ':')
	{
	    rw.rw_board  = getdigit(ap->a_disp[0]);
	    rw.rw_module = getdigit(ap->a_disp[1]);
	    rw.rw_addr   = a;
	}
	else if (isalnum(ap->a_disp[0]) && ap->a_disp[1] == ':')
	{
	    rw.rw_board  = getdigit(ap->a_disp[0]);
	    rw.rw_module = 0;
	    rw.rw_addr   = a;
	}
	else
	{
	    rw.rw_board  = def_board;
	    rw.rw_module = def_module;
	    rw.rw_addr   = a;
	}

	memcpy((char *)rw.rw_data, (char *)mem, size);

	stio.ic_cmd = DIGI_KME;
	stio.ic_timout = 0;
	stio.ic_len = sizeof(rw);
	stio.ic_dp = (char *) &rw;

	if (ioctl(fepfd, I_STR, (char *)&stio) == -1 ||
	     rw.rw_size != size)
	    return(0);

	return(1);
    }
#endif /* HAVE_STROPTS_H */

    /*
     *	Write using DigiBoard special device driver hooks.
     */

    if (fepdev)
    {
	static rw_t rw;
	register a_field *ap;

	rw.rw_req   = RW_WRITE;
	rw.rw_size  = size;

	for (ap = &afield[irow];ap->a_disp == 0;--ap) {}

	if (isalnum(ap->a_disp[0]) &&
	     isalnum(ap->a_disp[1]) &&
	     ap->a_disp[2] == ':')
	{
	    rw.rw_board  = getdigit(ap->a_disp[0]);
	    rw.rw_module = getdigit(ap->a_disp[1]);
	    rw.rw_addr   = a;
	}
	else if
	    (isalnum(ap->a_disp[0]) &&
	     ap->a_disp[1] == ':')
	{
	    rw.rw_board  = getdigit(ap->a_disp[0]);
	    rw.rw_module = 0;
	    rw.rw_addr   = a;
	}
	else
	{
	    rw.rw_board  = def_board;
	    rw.rw_module = def_module;
	    rw.rw_addr   = a;
	}

	memcpy((char *)rw.rw_data, (char *)mem, size);

	if (ioctl(fepfd, DIGI_KME, (char *)&rw) == -1 ||
	     rw.rw_size != size)
	    return(0);

	return(1);
    }

#if HAVE_SOCKET
    if (nsoc && use_gdb)
    {
	char cmd[512];
	char resp[512];
	int fd = socfd[ov_board];
	int len;
	char *cp;
	unsigned char *mp;

	cp = cmd + sprintf(cmd, "M%lx,%d:", a, size);
	mp = (unsigned char *) mem;
	while (size--)
		cp += sprintf(cp, "%02x", *mp++);

	gdb_send(fd, cmd);

	len = gdb_rcv(fd, resp);
	if (strcmp(resp, "OK") == 0)
		return 1;
	else
		return 0;
    }

    if (nsoc && !use_gdb)
    {
	int fd;
	int rtry;
	int wtry;
	rw_t w;
	struct timeval tv;

	static fd_set fdn;
	static fd_set fdr;
	static rw_t r;
	register a_field *ap;

	if (ov_board >= nsoc) return(0);

	/*
	 *  Build the request packet.
	 */

	w.rw_req   = RW_WRITE;
	w.rw_size  = size;

	for (ap = &afield[irow];ap->a_disp == 0;--ap) {}

	if (isalnum(ap->a_disp[0]) &&
	     isalnum(ap->a_disp[1]) &&
	     ap->a_disp[2] == ':')
	{
	    w.rw_board  = getdigit(ap->a_disp[0]);
	    w.rw_module = getdigit(ap->a_disp[1]);
	    w.rw_addr   = a;
	}
	else if
	    (isalnum(ap->a_disp[0]) &&
	     ap->a_disp[1] == ':')
	{
	    w.rw_board  = getdigit(ap->a_disp[0]);
	    w.rw_module = 0;
	    w.rw_addr   = a;
	}
	else
	{
	    w.rw_board  = def_board;
	    w.rw_module = def_module;
	    w.rw_addr   = a;
	}

	w.rw_size = swaps(w.rw_size);
	w.rw_addr = swapl(w.rw_addr);

	memcpy((char *)w.rw_data, (char *)mem, size);

	/*
	 *  Repeatedly request the data with UDP until
	 *  a response is heard from the remote.
	 */

	fd = socfd[w.rw_board];

	for (wtry = 0;wtry < 1;wtry++)
	{
	    write(fd, (char *)&w, RW_PAKLEN);

	    /*
	     *  Wait for the response packet.
	     */

	    for (rtry = 0;rtry < 3;rtry++)
	    {
		FD_ZERO(&fdr);
		FD_SET(fd, &fdr);

		tv.tv_sec = 2;
		tv.tv_usec = 0;

		if (select(FD_SETSIZE, &fdr, &fdn, &fdn, &tv) < 1)
		    break;

		/*
		 *  On response from the correct file descriptor,
		 *  with the response we expect, accept the response.
		 *
		 *  Note that it is possible to get an irrelevant
		 *  response if on a previous request, the network
		 *  delays exceeded our simplistic 2 second timeout.
		 */

		if (FD_ISSET(fd, &fdr) &&
		     read(fd, (char *)&r, RW_PAKLEN) == RW_PAKLEN &&
		     r.rw_req    == w.rw_req &&
		     r.rw_board  == w.rw_board &&
		     r.rw_module == w.rw_module &&
		     r.rw_addr   == w.rw_addr &&
		     r.rw_size   == w.rw_size)
		{
		    return(1);
		}
	    }
	}

	return(0);
    }
#endif

#if HAVE_MMAP
    if (memlen)
    {
	uchar *addr = memva + a;

	switch (size)
	{
	case 1:
	    *(char *) addr = *(char *) mem;
	    break;
	case 2:
	    if (ALIGNED(addr,2) && ALIGNED(mem,2))
		*(short *) addr = *(short *) mem;
	    else
		memcpy(addr, (char *) mem, (unsigned) size);
	    break;
	case 4:
	    if (ALIGNED(addr,4) && ALIGNED(mem,4))
		*(long *) addr = *(long *) mem;
	    else
		memcpy(addr, (char *) mem, (unsigned) size);
	    break;
	default:
	    memcpy(addr, (char *) mem, (unsigned) size);
	    break;
	}
	return (1);
    }
#endif

#if HAVE_PTRACE
    if (pid)
    {
	int i;

	if (!stopchild())
	    return 0;

	for (i = 0; i < size - (int) sizeof(int); i += sizeof(int))
	{
	    ptrace(PTRACE_POKEDATA, pid, a + i, *(int*)((void*)mem+i));
	    if (errno)
		return 0;
	}

	if (i < size)
	{
	    int b;
	    b = ptrace(PTRACE_PEEKDATA, pid, a + i, 0);
	    if (errno)
		return 0;
	    memcpy(&b, (void*)mem + i, size - i);
	    ptrace(PTRACE_POKEDATA, pid, a + i, b);
	    if (errno)
		return 0;
	}

	return 1;
    }
#endif

    /*
     *	Write to memory device.
     */

    if (lseek(memfd, a, SEEK_SET) != a ||
	write(memfd, (char *)mem, (unsigned) size) != size)
    {
	return(0);
    }

    if (debug)
	fprintf(stderr,
		"Wrote %lx to %lx (%d bytes)\n",
		v, a, size);

    return(1);
}


/************************************************************************
 *  inshell - Input shell.
 ************************************************************************/

int
inshell(char* buf)
{
    register int i;
    register char *sh;
    register int child;
    int status;
    void (*sig[3])();

    if (debug) fprintf(stderr, "executing: %s\n", buf);

    move(LINES-1, 0);
    refresh();

#if OLDCURSE
#if HAVE_TERMIOS_H
    tcsetattr(0, TCSANOW, &otio);
#else
    ioctl(0, TCSETA, &otio);
#endif
#else
    reset_shell_mode();
#endif

    putchar('\n');
    fflush(stdout);

    /*
     * Disable child interrupts.
     */

    signal(SIGCLD, SIG_DFL);

    /*
     *	Fork off a child process to become the shell.
     *	If we have problems getting a process, keep
     *	trying.
     */

    sigint = 0;

    while ((child = fork()) == -1)
    {
	if (sigint)
	{
	    fprintf(stderr, "Couldn't fork!");
	    sleep(2);
	    goto done;
	}
	sleep(1);
    }

    /*
     *	With the child process, execute a shell.
     */

    if (child == 0)
    {
	for (i = 3;i < 10;i++) close(i);

	sh = getenv("SHELL");
	if (sh == 0) sh = "/bin/sh";
	execl(sh, "sh", "-c", buf, (char *)0);

	perror(sh);
	exit(0xff);
    }

    /*
     *  Inibit interrupts while we wait for the child
     *	to complete.
     */

    signal(SIGINT, SIG_IGN);

    sig[1] = signal(SIGQUIT, SIG_IGN);
    sig[2] = signal(SIGTERM, SIG_IGN);

#if HAVE_PTRACE
    signal(SIGCLD, SIG_DFL);

    for (;;)
    {
	int wid = wait(&status);

	if (wid == child)
	    break;
	
	if (wid == pid)
	    handle_kid_status(status);
    }

    sigaction(SIGCLD, &sigaction_child, 0);
#else
    while (wait(&status) != child);
#endif	    

    printf("Command done!\n");

    sigaction(SIGINT, &sigaction_int,   0);

    signal(SIGQUIT, sig[1]);
    signal(SIGTERM, sig[2]);

    /*
     *	Restore tty modes.
     */

 done:
    fputc('\r', stdout);
    fflush(stdout);

#if OLDCURSE
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;
#if HAVE_TERMIOS_H
    tcsetattr(0, TCSADRAIN, &tio);
#else
    ioctl(0, TCSETAW, &tio);
#endif
#else
    cbreak();
#endif

    clrtoeol();
    standout();
    addstr("[Hit return to continue] ");
    standend();
    refresh();

    readch();

    clearok(stdscr, 1);

    return(1);
}


/************************************************************************
 *  inprint - Input print file name, and save screen
 ************************************************************************/

static char last_print[80] = "| lpr"; /* A good default */

int
inprint(char* buf)
{
    register FILE *file;
    int oldx, oldy;
    int x, y, cols;
    int pipe = 0;
    int append = 0;
    char *obuf = buf;

    /*
     * Parse filename for append and pipe options
     */
    if (buf[0] == '|')
    {
	pipe = 1;
	++buf;
    }
    else if (buf[0] == '>')
    {
	++buf;
	if (buf[0] == '>')
	{
	    ++buf;
	    append = 1;
	}
    }

    while (*buf == ' ' || *buf == '\t') ++buf;
    if (*buf == 0) return(0);

    /*
     *	Open output file or pipe.
     */
    if (pipe)
	file = popen(buf, "w");
    else
	file = fopen(buf, append ? "a" : "w");
    if (!file) return (0);

    strncpy(last_print, obuf, sizeof(last_print) -1);

    getyx(stdscr, oldy, oldx);

    /*
     *  Print the screen
     */
    for (y = 0; y < LINES-1; ++y)
    {
	for (cols = COLS-1; cols; --cols)
	    if ((mvinch(y, cols) & A_CHARTEXT) != ' ')
		break;
	++cols;
	for (x = 0; x < cols; ++x)
	    fputc(mvinch(y, x) & A_CHARTEXT, file);
	fputc('\n', file);
    }

    move(oldy, oldx);

    if (pipe)
	return(pclose(file) == 0);
    else
	return(fclose(file) == 0);
}


/************************************************************************
 *  insave - Input save file name, and save user-entered
 *	     stuff into a command file.
 ************************************************************************/

static char last_saved[80]; /* So we can edit later */

int insave(char* buf)
{
    register FILE *file;
    register a_field *ap;
    register int ch;
    register int i;
    register char *s;

    /*
     *	Open output file.
     */

    i = open(buf, O_WRONLY|O_CREAT|O_TRUNC, 0777);

    if (i == -1)
	return(0);

    file = fdopen(i, "w");
    if (file == 0)
	return(0);

    strncpy((char *)last_saved, buf, sizeof(last_saved) -1);

    /*
     *	Output command name and options.
     */

    fprintf(file, "exec %s", progname);

    if (!addrflag)
	fprintf(file, " -a");

    if (base)
	fprintf(file, " -b %lx", base);

    if (coredev)
	fprintf(file, " -c %s", coredev);

    if (elfcore)
	fprintf(file, " -C %s", elfcore);

    if (fepdev)
	fprintf(file, " -f %s", fepdev);

    if (defspath && defspath != defspath_default)
	fprintf(file, " -D %s", defspath);

    if (defsname && defsname != defsname_default)
	fprintf(file, " -d %s", defsname);

    if (helpflag)
	fprintf(file, " -h");

    if (idline)
	fprintf(file, " -i");

    if (kid_signal)
	fprintf(file, " -I %d", kid_signal);

    if (def_board || def_module)
    {
	fprintf(file, " -k %c", hexnum[def_board]);
	if (def_module) fprintf(file, "%c", hexnum[def_module]);
    }

#if HAVE_SOCKET
    if (hostname)
    {
	if (use_gdb)
	    fprintf(file, " -g %s", hostname);
	else
	    fprintf(file, " -m %s", hostname);
    }
#endif
#if HAVE_MMAP
    if (memlen)
	fprintf(file, " -M 0x%lx:0x%lx", mempa, memlen);
#endif


    if (symparam)
	fprintf(file, " -n %s", symparam);

    if (frow)
	fprintf(file," -p %d", frow + 1);

#if HAVE_PTRACE
    if (pid != 0)
	fprintf(file, " -P %d", pid);
#endif

    if (writeflag == 0)
	fprintf(file, " -r");

    if (swapflag)
	fprintf(file, " -s");

    if (addr_mask != -1)
	fprintf(file, " -S 0x%lx", addr_mask);

    if (!index_mode)
	fprintf(file, " -t");

    if (uptime != 2)
	fprintf(file, " -u %d", uptime);

#if HAVE_SOCKET
    if (udp_port != 0)
	fprintf(file, " -U %d", udp_port);
#endif

    if (colwidth != COLWIDTH)
	fprintf(file, " -w %d", colwidth);

    fprintf(file, " $*");

    /*
     *	Output keyboard entered defines.
     */

    for (i = 0;i < 26;i++)
    {
	if (keyformat[i] && (s = format[i]))
	{
	    fprintf(file, "\t\\\n\t%c=", i + 'A');
	    while ((ch = *s++))
	    {
		if (!isalnum(ch)) fputc('\\', file);
		fputc(ch, file);
	    }
	}
    }

    /*
     *	Output field defines.
     */

    ap = &afield[0];
    for (i = 0;i < NADR;i++)
    {
	if (ap->a_disp)
	{
	    fprintf(file, "\t\\\n\t%d=", i+1);

	    for (s = ap->a_disp;(ch = *s++);)
	    {
		if (!isalnum(ch)) fputc('\\', file);
		fputc(ch, file);
	    }
	}
	ap++;
    }

    fputc('\n', file);

    return(fclose(file) == 0);
}


/************************************************************************
 *  inuptime - Input update time.
 ************************************************************************/

int
inuptime(char* buf)
{
    int u;

    if (sscanf(buf, "%d", &u) != 1)
	return(0);

    uptime = u;

    return(1);
}


/************************************************************************
 *  insearch - Input search string.
 *
 *  NOTE: full semantics of / not yet implemented.  This
 * version doesn't search format text, nor does it do REs.
 ************************************************************************/

static char	last_search[80];
int		search_dir;

int
insearch(char* buf)
{
    int		i, r;
    a_field	*ap;
    char	*p;

    strncpy((char *)last_search, buf, sizeof(last_search) - 1);

    r = irow + search_dir;
    for (i = 0; i < NADR; ++i, r += search_dir)
    {
	if (r == NADR)
	    r = 0;
	if (r < 0)
	    r = NADR - 1;

	ap = &afield[r];

	if (!ap->a_disp)
	    continue;

	/*
	 * Search in address expression and top level format
	 */
	if (strstr(ap->a_disp, buf))
	    break;

	/*
	 * Extremely simplistic seach thru definition of first !format
	 *
	 * This is so incomplete and badly handled I'm embarrassed.
	 * Yet, it solves the immediate problem I have.
	 */
	p = strchr(ap->a_disp, '!');
	if (p)
	{
	    char	fbuf[80];
	    char	*fp = fbuf;
	    char	*sp = fbuf;
	    int		x;
	    int		state;
	    int		offset;

	    ++p;
	    while (isalnum(*p) || (*p == '_'))
	       	*fp++ = *p++;
	    *fp = 0; 
	    x = find_format(fbuf);
	    if (x != -1 && dfmt[x].format && (sp = strstr(dfmt[x].format, buf)))
	    {
		state = 0;
		offset = 1;
		for (fp = dfmt[x].format; fp < sp; ++fp)
		{
		    switch (state)
		    {
		    case 0:
			if (*fp == '"')
			    state = 1;
			else if (*fp == 'n')
			    ++offset;
			break;
		    case 1:
			if (*fp == '"')
			    state = 0;
			break;
		    }
		}
		r += offset;
		if (r >= NADR)
		    r = NADR - 1;
		break;
	    }
	}
    }

    if (i != NADR)
    {
	irow = trow = r;
	lrow = -1;
    }
    else
	beep();

    return (1);
}


/************************************************************************
 *  readch - Read an input character in delay mode.
 ************************************************************************/

int readch(void)
{
    for (;;)
    {
#if OLDCURSE
	char buf[1];

	if (read(0, buf, 1) == 1)
	    return(buf[0]);
#else
	int ch = getch();
	if (ch != ERR)
	    return(ch);
#endif
    }
}


/************************************************************************
 *  collect - Collect characters into a field.
 ************************************************************************/

void 
collect(int row, int col, char* buf, int width, int (*proc)())
{
    register int i;
    register int ch;
    register int startpos;
    register int len;

    for (len = 0;buf && *buf && len < LINELEN;len++)
	line[len] = *buf++;

    line[len] = 0;

#if 0
    /*
     *  If no edit string is given, go into insert mode with
     *  a blank line.  If an edit string is given, position
     *  the cursor to the end of line, and go into edit mode
     *  immediately.
     */

    if (buf)
	edit_mode();
    else
	insert_mode();
#else
    /*
     * It seems to be easier for the unwashed masses to handle
     * if you always start in edit mode.
     */

    insert_mode();
#endif

    linepos = len;
    startpos = 0;

    lineproc = proc;

    /*
     *	Loop to edit keystroke input.
     */

#if OLDCURSE
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;
#if HAVE_TERMIOS_H
    tcsetattr(0, TCSADRAIN, &tio);
#else
    ioctl(0, TCSETAW, &tio);
#endif
#else
    cbreak();
#endif

    for (;;)
    {
	/*
	 *  Display current field, scrolling if necessary so the
	 *  complete input line can be displayed.
	 */

	move(row, col);

	len = strlen(line);

	if (startpos <= linepos - width) startpos = linepos - width + 1;
	if (startpos > linepos) startpos = linepos;

	for (i = startpos;i < startpos + width;i++)
	{
	    if (i < len) addch((chtype) line[i]);
	    else addch((chtype) mode_ind);
	}

	/*
	 *  Read next input character.
	 */

	move(row, col + linepos - startpos);
	refresh();

	ch = readch();

	switch (ch)
	{
	    /*
	     *  Handle screen refresh special.
	     */

#if KEY_REFRESH
	case KEY_REFRESH:
#endif
	case ctrl('l'):
	case ctrl('r'):
	    clearok(stdscr, 1);
	    break;

	    /*
	     *  Everything else is a VI command.
	     */

	default:
	    update_line(ch);
	    break;
	}

	if (linepos == -1) break;
    }
}

/************************************************************************
 *  do_pause - Pause KME screen refresh: display msg and wait for key.
 ************************************************************************/

void 
dopause(const char *msg)
{
    static const char defmsg[] = " - Hit Return to Continue ";

    move(LINES-1, 0);
    clrtoeol();
    move(LINES-1, (COLS-strlen(defmsg)-strlen(msg)) / 2);
    standout();
    printw(msg);
    printw(defmsg);
    standend();
    refresh();

#if OLDCURSE
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;
#if HAVE_TERMIOS_H
    tcsetattr(0, TCSADRAIN, &tio);
#else
    ioctl(0, TCSETAW, &tio);
#endif
#else
    cbreak();
#endif
    readch();

    lrow = -1;
}

/************************************************************************
 *  docmd - Process command keystrokes.
 ************************************************************************/

void docmd()
{
    register int ch;
    register a_field *ap;
    register a_field *bp;
    int r;
    int m;
    static int count;

    /*
     *	Position the cursor, and flush output.
     */

    move(FROW(irow), FCOL(icol));
    refresh();

#if OLDCURSE
    if (uptime == 0)
    {
	tio.c_cc[VMIN] = 1;
	tio.c_cc[VTIME] = 0;
    }
    else
    {
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = uptime;
    }
#if HAVE_TERMIOS_H
    tcsetattr(0, TCSADRAIN, &tio);
#endif
#else
    if (uptime == 0)
	cbreak();
    else
	halfdelay(uptime);
#endif

#ifdef HAVE_SYS_TIME_H
    struct timeval start;

    if (gettimeofday(&start, 0) != 0)
	perror("gettimeofday");
#endif    

    /*
     *	Process the characters received.
     */

    trow = frow;

    for (;;)
    {
#if OLDCURSE
	char buf[1];
	
	if (read(0, buf, 1) == 1)
	    ch = buf[0];
	else
	    ch = ERR;
#else	
	ch = getch();
#endif
	if (ch == ERR && !requested_kid_pause)
	{
#ifdef HAVE_SYS_TIME_H
	    struct timeval now;
	    ulong t;

	    if (uptime == 0)
		continue;

	    if (gettimeofday(&now, 0) != 0)
		perror("gettimeofday");

	    t = ((long) (now.tv_sec - start.tv_sec) * 10 +
		 (long) (now.tv_usec - start.tv_usec) / 100000);

	    if (t < uptime)
		continue;
#endif
	    goto done;
	}

	if ((count ? '0' : '1') <= ch && ch <= '9')
	{
	    if (count > 999) count = 999;
	    count = 10 * count + ch - '0';
	    continue;
	}

	if (isupper(ch))
	    ch = tolower(ch);

	switch (ch)
	{

	    /*
	     *  Redraw screen.
	     */

#if KEY_REFRESH
	case KEY_REFRESH:
#endif
	case ctrl('l'):
	case ctrl('r'):
	    clearok(stdscr, 1);
	    lrow = -1;
	    break;

	    /*
	     * Toggle between index formats (byte and array index)
	     */

	case ctrl('t'):
	    index_mode ^= 1;
	    break;

	    /*
	     *  Next page.
	     */

#if KEY_NPAGE
	case KEY_NPAGE:
#endif
	case ctrl('F'):
	    if (count == 0) count = 1;
	    r = trow + (nrow-1) * count;
	    if (r > NADR - nrow) r = NADR - nrow;
	    if (r == trow) beep();
	    irow += r - trow;
	    trow = r;
	    break;

	    /*
	     *  Scroll down.
	     */

#if KEY_SF
	case KEY_SF:
#endif
	case ctrl('D'):
	    if (count) scount = count;
	    r = trow + scount;
	    if (r > NADR - nrow) r = NADR - nrow;
	    if (r == trow) beep();
	    irow += r - trow;
	    trow = r;
	    break;

	    /*
	     *  Previous page.
	     */

#if KEY_PPAGE
	case KEY_PPAGE:
#endif
	case ctrl('B'):
	    if (count == 0) count = 1;
	    r = trow - (nrow-1) * count;
	    if (r < 0) r = 0;
	    irow += r - trow;
	    if (r == trow) beep();
	    trow = r;
	    break;

	    /*
	     *  Scroll up.
	     */

#if KEY_SR
	case KEY_SR:
#endif
	case ctrl('U'):
	    if (count) scount = count;
	    r = trow - scount;
	    if (r < 0) r = 0;
	    irow += r - trow;
	    if (r == trow) beep();
	    trow = r;
	    break;

	    /*
	     *  Next line.
	     */

	case '\r':
	case '\n':
	    icol = 0;
	    if (irow >= NADR - 1) goto error;
	    irow += count ? count : 1;
	    if (irow >= NADR) irow = NADR - 1;
	    if (irow >= trow + nrow) trow = irow - nrow + 1;
	    break;

	    /*
	     *  Shell escape.
	     */

#if KEY_COMMAND
	case KEY_COMMAND:
#endif
	case '!':
	case ':':
	    move(LINES-1, 0);
	    clrtoeol();
	    printw("shell:");
	    collect(LINES-1, 7, (char *)0, COLS-8, inshell);
	    lrow = -1;
	    goto flush;

	    /*
	     *  Help on/off.
	     */

#if KEY_HELP
	case KEY_HELP:
#endif
	case '?':
	    if (helpflag ^= 1)
	    {
		nrow--;
		if (irow >= trow + nrow) irow--;
	    }
	    else
	    {
		nrow++;
		if (trow > NADR - nrow) trow--;
		lrow = -1;
	    }
	    break;

	    /*
	     *  Mark this address block as the indirect target.
	     */

	case '=':
	    indirect_row = (indirect_row == irow) ? -1 : irow;
	    lrow = -1;
	    break;

	    /*
	     *  Indirect to through contents of current cell.
	     */

	case '*':
	    if (addrflag && afield[irow].a_disp != 0)
	    {
		/* Cancel the effect */

		ap = &afield[irow];
		addr = 0;
	    }
	    else
	    {
		/* Go indirect now */

		if (icol == 0)
		{
		    /* Use the hex address field */

		    addr = afield[irow].a_eaddr;
		    if (addr == 0)
			goto error;
		}
		else
		{
		    /* Use data field contents */

		    d_field *dp = &DFIELD(irow, icol);

		    addr = dp->d_addr;

		    switch (dp->d_type)
		    {
		    case 'x':
			if (getmem(2) == 0)
			    goto error;
			addr = *(kme_uint16_t*)mem;
			break;

		    case 'l':
			if (getmem(sizeof(kme_uint32_t)) == 0)
			    goto error;
			addr = *(kme_uint32_t*)mem;
			break;

#if CC_HAS_LONG_LONG
		    case 'q':
			if (getmem(sizeof(kme_uint64_t)) == 0)
			    goto error;
			addr = *(kme_uint64_t*)mem;
			break;
#endif

		    default:
			goto error;
		    }
		}

		/* Find target field to modify */

		ap = &afield[indirect_row > 0 ? indirect_row : irow];

		for (; ap->a_disp == 0; ap--)
		    if (ap == afield) goto error;

		if (getmem(1) == 0)
		    goto error;
	    }

	    /* Set the indirect row and reset the offset */

	    ap->a_iaddr = addr;
	    ap->a_offset = 0;
	    break;

	    /*
	     *  Backward in memory.
	     */

#if KEY_PREVIOUS
	case KEY_PREVIOUS:
#endif
	case '-':
	    for (ap = &afield[irow]; ap->a_disp == 0; ap--)
	    {
		if (ap == afield) goto error;
	    }

	    if (ap->a_size == 0) goto error;

	    if (count == 0) count = 1;
	    ap->a_offset -= count * ap->a_size;
	    break;

	    /*
	     *  Forward in memory.
	     */

#if KEY_NEXT
	case KEY_NEXT:
#endif
	case '+':
	    for (ap = &afield[irow]; ap->a_disp == 0; ap--)
	    {
		if (ap == afield) goto error;
	    }

	    if (ap->a_size == 0) goto error;

	    if (count == 0) count = 1;
	    ap->a_offset += count * ap->a_size;
	    break;

	    /*
	     *  Start of line.
	     */

	case '0':
	case '^':
	    if (icol == 0) goto error;
	    icol = 0;
	    break;

	    /*
	     *  Toggle address display mode.
	     */
	
	case 'a':
	    addrflag = !addrflag;
	    break;

	    /*
	     *  Enter base address.
	     */

	case 'b':
	    {
		char base_asc[11];
		sprintf(base_asc, "0x%lx", base);
		move(LINES-1, 0);
		clrtoeol();
		printw("base address:");
		collect(LINES-1, 14, base_asc, 8, inbase);
		lrow = -1;
		goto flush;
	    }

	    /*
	     *  Change field.
	     */

#if KEY_REPLACE
	case KEY_REPLACE:
#endif
	case 'c':
	    if (icol == 0)
	    {
		showhelp(1);
		collect(FROW(irow), FCOL(0), (char *)0,
			COLS - FCOL(0) - 1, inaddr);
	    }
	    else {
		int dtype = DFIELD(irow, icol).d_type;

		if (dtype == 0 || !writeflag)
		    goto error;

		showhelp(2);

		switch (DFIELD(irow, icol).d_type)
		{
		case 'g':
		case 'q':
		    collect(FROW(irow), FCOL(icol), (char *)0,
			    2*colwidth, indata);
		    break;
		default:
		    collect(FROW(irow), FCOL(icol), (char *)0,
			    colwidth, indata);
		    break;
		}
	    }
	    goto flush;

	    /*
	     *  Delete rows.
	     */

#if KEY_DL
	case KEY_DL:
#endif
	case 'd':
	    if (count == 0) count = 1;

	    r = 0;
	    bp = &bfield[0];
	    while (r < brow)
	    {
		strfree(bp[0].a_disp);
		bp[0].a_offset = 0;
		bp[0].a_disp = 0;
		bp[0].a_size = 0;
		r++;
		bp++;
	    }

	    r = irow;
	    ap = &afield[irow];
	    bp = &bfield[0];
	    while (r < NADR)
	    {
		if (r < irow+count) bp[0] = ap[0];

		if (r < NADR-count)
		    ap[0] = ap[count];
		else
		{
		    ap[0].a_offset = 0;
		    ap[0].a_disp = 0;
		    ap[0].a_size = 0;
		}

		r++;
		ap++;
		bp++;
	    }

	    brow = count;
	    break;

	    /*
	     *  Edit field.
	     */

	case 'e':
	    ap = &afield[irow];
	    if (icol == 0)
	    {
		showhelp(1);
		collect(FROW(irow), FCOL(0), ap->a_disp ? ap->a_disp : "",
			COLS - FCOL(0) - 1, inaddr);
	    }
	    else if (DFIELD(irow, icol).d_type)
	    {
		if (!writeflag) goto error;

		showhelp(2);

		switch (DFIELD(irow, icol).d_type)
		{
		case 'g':
		case 'q':
		    collect(FROW(irow), FCOL(icol), "",
			    2 * colwidth, indata);
		    break;
		default:
		    collect(FROW(irow), FCOL(icol), "",
			    2 * colwidth, indata);
		    break;
		}
	    }
	    else goto error;
	    goto flush;

	    /*
	     *  Go to line number.
	     */

#if KEY_FIND
	case KEY_FIND:
#endif
#if KEY_REFERENCE
	case KEY_REFERENCE:
#endif
	case 'g':
	    if (count == 0) irow = NADR - 1;
	    else if (count > NADR) goto error;
	    else irow = count - 1;
	    if (irow < trow) trow = irow;
	    if (irow >= trow + nrow) trow = irow - nrow + 1;
	    break;

	    /*
	     *  Move left.
	     */

#if KEY_LEFT
	case KEY_LEFT:
#endif
	case 'h':
	    r = irow * (ncol + 1) + icol;
	    if (r == 0) goto error;
	    r -= count ? count : 1;
	    if (r < 0) r = 0;
	    irow = r / (ncol + 1);
	    icol = r % (ncol + 1);
	    if (trow > irow) trow = irow;
	    break;

	    /*
	     *  Insert rows.
	     */

#if KEY_OPEN
	case KEY_OPEN:
#endif
	case 'i':
	    insertscreenline(count);
	    break;

	    /*
	     *  Move down.
	     */

#if KEY_DOWN
	case KEY_DOWN:
#endif
	case 'j':
	    if (irow >= NADR - 1) goto error;
	    irow += count ? count : 1;
	    if (irow >= NADR) irow = NADR - 1;
	    if (irow >= trow + nrow) trow = irow - nrow + 1;
	    break;

	    /*
	     *  Move up.
	     */

#if KEY_UP
	case KEY_UP:
#endif
	case 'k':
	    if (irow == 0) goto error;
	    irow -= count ? count : 1;
	    if (irow < 0) irow = 0;
	    if (irow < trow) trow = irow;
	    break;

	    /*
	     *  Move right.
	     */

#if KEY_RIGHT
	case KEY_RIGHT:
#endif
	case ' ':
	case 'l':
	    r = irow * (ncol + 1) + icol;
	    m = NADR * (ncol + 1) - 1;
	    if (r == m) goto error;
	    r += count ? count : 1;
	    if (r > m) r = m;
	    irow = r / (ncol + 1);
	    icol = r % (ncol + 1);
	    if (irow >= trow + nrow) trow = irow - nrow + 1;
	    break;

	    /*
	     *  Define format macro.
	     */

#if KEY_CREATE
	case KEY_CREATE:
#endif
	case 'm':
	    move(LINES-1, 0);
	    clrtoeol();
	    printw("macro:");
	    r = irow;
	    collect(LINES-1, 7, (char *)0, COLS-8, inparam);
	    irow = r;
	    lrow = -1;
	    goto flush;
	
	    /*
	     *  Get namelist file.
	     */

	case 'n':
	    if (!readsym()) beep();
	    break;

	    /*
	     *  Put deleted lines back in.
	     */

#if KEY_IL
	case KEY_IL:
#endif
	case 'p':
	    if (brow == 0) goto error;

	    r = NADR;
	    ap = &afield[NADR];
	    bp = &bfield[NADR-irow];
	    while (r > irow)
	    {
		--r;
		--ap;
		--bp;

		if (r >= NADR-brow) strfree(ap[0].a_disp);

		if (r >= irow+brow) ap[0] = ap[-brow];
		else
		{
		    ap[0].a_offset = bp[0].a_offset;
		    ap[0].a_disp = stralloc(bp[0].a_disp);
		    ap[0].a_size = bp[0].a_size;
		}
	    }
	    break;

	    /*
	     *	Output screen to a file
	     */

	case 'o':
	    move(LINES-1, 0);
	    clrtoeol();
	    printw("print file:");

	    collect(LINES-1, 12, last_print,  COLS-13, inprint);
	    lrow = -1;
	    goto flush;

#if 0 /* letter already used, waaaaaa */
	case '?':
	    search_dir = -1;
	    goto search;
#endif

	case '/':
	    search_dir = 1;
	    goto search;

	search:
	    move(LINES-1, 0);
	    clrtoeol();
	    printw("search:");

	    collect(LINES-1, 8, last_search,  COLS-8-1, insearch);
	    lrow = -1;
	    goto flush;

	    /*
	     *  Quit.
	     */

#if KEY_EXIT
	case KEY_EXIT:
#endif
	case 'q':
	    quitreq = 1;
	    goto flush;

	    /*
	     *  Re-read defines file.
	     */

#if KEY_REDO
	case KEY_REDO:
#endif
	case 'r':
	    readdefs();
	    break;

	    /*
	     *  Save configuration.
	     */

#if KEY_SAVE
	case KEY_SAVE:
#endif
	case 's':
	    move(LINES-1, 0);
	    clrtoeol();
	    printw("save file:");

	    /* 
	       If not already saved, default to "insert/append" mode, 
	       otherwise, let them edit current prompt 
	    */
	    if (last_saved[0])
		collect(LINES-1, 11, last_saved,  COLS-12, insave);
	    else
		collect(LINES-1, 11, (char *)0, COLS-12, insave);
	    lrow = -1;
	    goto flush;

	    /*
	     *  Set update time.
	     */

	case 'u':
	    {
		char update_asc[8];
		sprintf(update_asc, "%d", uptime);
		move(LINES-1, 0);
		clrtoeol();
		printw("update interval (1/10ths of sec):");
		
		collect(LINES-1, 34, update_asc, 4, inuptime);
		lrow = -1;
		goto flush;
	    }
	
	    /*
	     *  Pause update.
	     */
	
	case 'w':
	    dopause("Manually paused");
	    goto done;

	    /*
	     *  Yank lines into buffer.
	     */

#if KEY_COPY
	case KEY_COPY:
#endif
	case 'y':
	    if (count == 0) count = 1;

	    r = 0;
	    bp = &bfield[0];
	    while (r < brow)
	    {
		strfree(bp[0].a_disp);
		bp[0].a_offset = 0;
		bp[0].a_disp = 0;
		bp[0].a_size = 0;
		r++;
		bp++;
	    }

	    r = 0;
	    ap = &afield[irow];
	    bp = &bfield[0];
	    while (r < count)
	    {
		bp[0].a_offset = ap[0].a_offset;
		bp[0].a_disp = stralloc(ap[0].a_disp);
		bp[0].a_size = ap[0].a_size;
		r++;
		ap++;
		bp++;
	    }

	    brow = count;
	    break;

	    /*
	     *  Zero field.
	     */

	case 'z':
	    if (!writeflag) goto error;

	    if (count == 0) count = 1;
	    if (icol == 0) icol = 1;

	    for (;;)
	    {
		if (DFIELD(irow, icol).d_type)
		{
		    if (count <= 0) break;
		    indata("0");
		    count--;
		}

		if (++icol > ncol)
		{
		    if (++irow >= trow + nrow)
		    {
			irow--;
			icol--;
			goto error;
		    }
		    icol = 1;
		}
	    }
	    break;

	    /*
	     *  Others are errors.
	     */

	default:
	    goto error;
	}

	count = 0;

	goto done;
    }

 error:
    beep();

 flush:
    count = 0;

 done:
    frow = trow;
}


/************************************************************************
 *  check_symread - See if the symbol table file has changed.  
 *                  If so, we silently reread it.
 *                  Optimizatino opportunities abound.
 ************************************************************************/

void
check_symreread()
{
    static struct stat symstat;
    static time_t last_time;

    /* If the file is gone now, we'll let someone else complain.
     * It may show back up on the next poll. */

    if (stat(symname, &symstat) < 0) 
    {	
	return;
    }

    if (symstat.st_mtime > last_time)
    {
	readsym();
	last_time = symstat.st_mtime;
    }
}


/************************************************************************
 *  mainloop - Update the screen and read keystroke commands.
 ************************************************************************/

void mainloop()
{
    register a_field *ap;
    register int arow;
    int err;
    int c;

    if (frow > NADR - nrow) frow = NADR - nrow;
    if (frow < 0) frow = 0;

    irow = frow;
    lrow = -1;
    indirect_row = -1;

    while (!quitreq)
    {
	laddr = haddr = 0;

	/*
	 *  Find the last row before the start of
	 *  the screen which has data to display.
	 */

	for (orow = frow;; orow--)
	{
	    if (orow < 0)
	    {
		orow = frow;
		break;
	    }

	    if (afield[orow].a_disp) break;
	}

	/*
	 *  Output data over the displayable region
	 *  of the screen only.
	 */

	crow = frow;

	while (crow < frow + nrow)
	{
	    erow = frow + nrow;
	    arow = crow;
	    ocol = 0;

	    /*
	     *  If a field begins on this line, process it, possibly
	     *  filling in subsequent lines until the data runs out,
	     *  the screen ends, or we run into another field.
	     */

	    ap = &afield[orow];

	    ap->a_eaddr = 0;

	    if (ap->a_disp)
	    {
		/*
		 *  Recognize board/concentrator prefix at beginning
		 *  of expression.
		 */
		
		str = ap->a_disp;

#if HAVE_SOCKET
		if (fepdev || nsoc)
		{
		    if (isalnum(str[0]) &&
			 isalnum(str[1]) &&
			 str[2] == ':')
		    {
			ov_board  = getdigit(str[0]);
			ov_module = getdigit(str[1]);
			str += 3;
		    }
		    else if
			(isalnum(str[0]) &&
			 str[1] == ':')
		    {
			ov_board  = getdigit(str[0]);
			ov_module = 0;
			str += 2;
		    }
		    else
		    {
			ov_board  = def_board;
			ov_module = def_module;
		    }
		}
#endif
		/*
		 *  Initialize the expression parser, to figure
		 *  out the address to display.
		 */

		--str;
		inparen = 0;

		err = 0;

		if (setjmp(efail))
		{
		    err++;
		}
		else
		{
		    temp[0] = ap->a_size;
		    ebase = 16;

		    addr = expr(0);

		    if (ap->a_iaddr != 0)
			addr = ap->a_iaddr;

		    addr = (addr + ap->a_offset) & addr_mask;
		    addr *= addrscale;

		    if (*str != '/') err++;
		}

		/*
		 *  On an error, show the incorrect display expression
		 *  in the first field of the line.
		 */

		if (err)
		{
		    if (orow >= frow)
		    {
			initline();

			c = *str;
			printw("%.*s", str - ap->a_disp, ap->a_disp);

			printw(" <ERROR> ");

			printw("%s", str);
		    }
		    orow++;
		}

		/*
		 *  If the addrflag is set, show the display
		 *  expression alone on the first line.
		 *
		 *  In any case, display the formatted data on
		 *  the next line.
		 */

		else
		{
		    if (addrflag)
		    {
			if (orow >= frow)
			{
			    initline();

			    if (ap->a_iaddr != 0)
				printw("%lx", ap->a_iaddr);
			    else
				printw("%.*s", str - ap->a_disp, ap->a_disp);

			    if (ap->a_offset < 0)
			    {
				if (index_mode)
				{
				    printw("-%lx", -ap->a_offset);
				}
				else
				{
				    printw("[-%ld]",
					   -(ap->a_offset/ap->a_size));
				}
			    }

			    if (ap->a_offset > 0)
			    {
				if (index_mode)
				    printw("+%lx", ap->a_offset);
				else
				    printw("[%ld]",
					   ap->a_offset/ap->a_size);
			    }

			    printw("%s", str);
			}

			orow++;
		    }
			
		    depth = 0;
		    faddr = addr;
		    rw_max = 128;

		    display(str+1);

		    ap->a_size = (addr - faddr) / addrscale;
		}

		move(FROW(irow), FCOL(icol));
		refresh();
	    }

	    if (crow == arow)
	    {
		clearline();
		crow++;
	    }

	    orow = crow;
	}

#if HAVE_PTRACE
	if (pid)
	    startchild();

	if (requested_kid_pause) {
		dopause("Signal captured");
		requested_kid_pause = !TRUE;
	}
#endif	

	lrow = frow;

	standend();

	showhelp(0);

	docmd();

	check_symreread();
    }
}


/************************************************************************
 *  beeper - Beep at operator.
 ************************************************************************/

#if OLDCURSE && 0
void
beeper()
{
    static char beepc = '\007';

    write(1, &beepc, 1);
}
#endif


/************************************************************************
 *  opensoc - Open socket interface.
 ************************************************************************/

#if HAVE_SOCKET
void
opensoc(char* str)
{
    int fd;
    int i;
    int ch;
    struct servent *sp;
    struct hostent *hp;
    struct sockaddr_in sin;
    char host[100];

    while (*str) {

	/*
	 * Extract the next entry in the colon separated
	 * list of host names.
	 */

	for (i = 0;*str && (ch = *str++) != ':';)
	{
	    if (i < sizeof(host)-1) host[i++] = ch;
	}
	host[i] = 0;

	if (nsoc >= MAXSOC)
	{
	    fprintf(stderr, "Too many sockets, increase MAXSOC\n");
	    exit(2);
	}

	/*
	 *  Open a socket, and bind it to a non-priveleged port.
	 */

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
	    perror("socket");
	    exit(2);
	}

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(0);

	if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
	    perror("connect");
	    exit(2);
	}

	/*
	 *  Get "kme" service port number.
	 */

	if (udp_port != 0)
	{
	    sin.sin_port = htons(udp_port);
	}
	else
	{
	    memset((char *)&sin, 0, sizeof(sin));

	    sp = getservbyname("kme", "udp");
	
	    if (sp != 0)
		sin.sin_port = sp->s_port;
	    else
		sin.sin_port = htons(UDP_PORT);
	}

	/*
	 *  Get destination address.
	 */

	sin.sin_addr.s_addr = inet_addr(host);

	if (sin.sin_addr.s_addr != -1)
	{
	    sin.sin_family = AF_INET;
	}
	else
	{
	    hp = gethostbyname(host);
	    if (hp == 0)
	    {
		fprintf(stderr, "unknown host: %s\n", host);
		exit(2);
	    }
	    sin.sin_family = hp->h_addrtype;
	    memcpy((char *)&sin.sin_addr, hp->h_addr, hp->h_length);
	}

	if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	    perror("connect");
	    exit(2);
	}

	socfd[nsoc] = fd;

	nsoc++;
    }
}
#endif

#if HAVE_SOCKET
int
opensocket(char *host, char *port, int tcp)
{
    int fd;
    struct hostent *hp;
    struct sockaddr_in sin;

    /*
     *  Open a socket
     */
    fd = socket(AF_INET, tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (fd < 0) {
	perror("socket");
	exit(2);
    }

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons( atoi(port) );

    /*
     *  Get destination address.
     */
    sin.sin_addr.s_addr = inet_addr(host);

    if (sin.sin_addr.s_addr != -1)
	sin.sin_family = AF_INET;
    else
    {
	hp = gethostbyname(host);
	if (hp == 0)
	{
	    fprintf(stderr, "unknown host: %s\n", host);
	    exit(2);
	}
	sin.sin_family = hp->h_addrtype;
	memcpy((char *)&sin.sin_addr, hp->h_addr, hp->h_length);
    }

    if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	perror("connect");
	exit(2);
    }

    return fd;
}
#else
int
opensocket(char *host, char *port, int tcp)
{
    return -1;
}
#endif

int
openserial(char *device, char *baud)
{
    int	fd;
    int cbaud;
    #if HAVE_TERMIOS_H
	struct termios tio;
    #else /* HAVE_TERMIOS_H */
	struct termio tio;
    #endif /* HAVE_TERMIOS_H */

    fd = open(device, 2);
    if (fd < 0) return fd;

    #if HAVE_TERMIOS_H
	tcgetattr(fd, &tio);
    #else
	ioctl(fd, TCGETA, &tio);
    #endif

    cbaud = B38400;
    #ifdef B230400
	if (strcmp(baud, "230400") == 0) cbaud = B230400;
    #endif
    #ifdef B115200
	if (strcmp(baud, "115200") == 0) cbaud = B115200;
    #endif
    #ifdef B57600
    if (strcmp(baud, "57600") == 0) cbaud = B57600;
    #endif
    if (strcmp(baud, "38400") == 0) cbaud = B38400;
    if (strcmp(baud, "19200") == 0) cbaud = B19200;
    if (strcmp(baud, "9600") == 0) cbaud = B9600;

    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;
    tio.c_iflag = IXON|IXOFF|ISTRIP;
    tio.c_oflag &= ~OPOST;
    tio.c_cflag = CLOCAL|CS8|CREAD|cbaud;
    tio.c_lflag = 0;

    #if HAVE_TERMIOS_H
	tcsetattr(fd, TCSANOW, &tio);
    #else
	ioctl(fd, TCSETA, &tio);
    #endif

    return fd;
}

void
open_gdb(char *str)
{
    int fd;
    int i;
    int ch;
    char host[100];
    char buf[512];
    char *arg;

    while (*str) {

	/*
	 * Extract the next entry in the comma separated
	 * list of host names.
	 */

	for (i = 0;*str && (ch = *str++) != ',';)
	{
	    if (i < sizeof(host)-1) host[i++] = ch;
	}
	host[i] = 0;

	if (nsoc >= MAXSOC)
	{
	    fprintf(stderr, "Too many sockets, increase MAXSOC\n");
	    exit(2);
	}

	arg = strchr(host, ':');
	if (arg)
		*arg++ = 0;

	if (host[0] == '/')
	{
	    /* Use a serial port device, e.g. /dev/ttyS0:115200 */
	    fd = openserial(host, arg ? arg : "38400");
	    if (fd < 0) {
		perror("socket");
		exit(2);
	    }
	}
	else
	{
	    /* Use a TCP/IP socket, e.g. my.target.com:9000 */
	    fd = opensocket(host, arg ? arg : gdb_port, TRUE);
	    if (fd < 0) {
		perror("socket");
		exit(2);
	    }
	}

	/*
	 * Flush any input we get in the next second,
	 * Ack any old messages,
	 * Send same first command (what does it do?) that GDB does.
	 * */
	gdb_flushin(fd);
	write(fd, "+", 1); if (debug) fprintf(stderr, "GDB send +\n");
	gdb_send(fd, "Hc-1");
	gdb_rcv(fd, buf);

	socfd[nsoc] = fd;

	nsoc++;
    }
}


/************************************************************************
 *  version_info - display confiration information.
 ************************************************************************/
void
version_info(void)
{
    char *vinfo[] = {
	PACKAGE " version " VERSION,

#if HAVE_MMAP
	"MMAP enabled",
#endif

#if HAVE_SOCKET
	"sockets enabled",
#endif

#if HAVE_NLIST
	"nlist enabled",
#endif

#if HAVE_PTRACE
	"ptrace enabled",
#endif

#if HAVE_LIBELF
	"libelf present",
#endif

#if HAVE_LIBDL
	"libdl present",
#endif
	NULL
    };

    char **v = vinfo;
    while (*v)
    {
	printf("%s\n", *v);
	v++;
    }
    exit(0);
}


/************************************************************************
 *  main - main program.
 ************************************************************************/

int
main(int argc, char** argv)
{
    char *cp;
    char ch;
    int c;

    /*
     *	Unpack options.
     */

    progname = argv[0];

    if ((cp = getenv("KME_DEFS")) != 0) defsname = cp;
    if ((cp = getenv("KME_PATH")) != 0) defspath = cp;
    if ((cp = getenv("KME_CORE")) != 0) corename = cp;
    if ((cp = getenv("KME_SYMS")) != 0) symname = cp;
   
    init_signal_names();

    fmt_cnt = dl_fmt_cnt = 0;

    while ((c = getopt(argc, argv,
		       "aA:b:c:C:d:D:f:g:hiI:k:L:m:M:n:p:P:rsS:tu:U:Vw:x")) != -1)
    {
	switch (c)
	{
	case 'a':
	    addrflag = !addrflag;
	    break;

	case 'A':
	    addrscale = atoi(optarg);
	    break;

	case 'b':
	    if (!inbase(optarg)) goto usage;
	    break;

	case 'c':
	    coredev = corename = optarg;
	    break;

	case 'C':
	    elfcore = optarg;
	    break;

	case 'd':
	    defsname = optarg;
	    break;

	case 'D':
	    defspath = optarg;
	    break;

	case 'f':
	    fepdev = optarg;
	    break;

	case 'g':
#if HAVE_SOCKET
	    corename = "";
	    hostname = optarg;
	    use_gdb = 1;
	    break;
#else
	    fprintf(stderr, "Networking support not compiled in!\n");
	    goto usage;
#endif

	case 'h':
	    helpflag = 1;
	    break;

	case 'i':
#if TERMINFO
	    idline ^= 1;
#else
	    fprintf(stderr,
		    "Note: insert/delete option not compiled in!\n");
#endif
	    break;
	case 'I':
	    kid_signal = find_signal_name(optarg);
	    if (-1 == kid_signal) {
	         kid_signal = atoi(optarg);
	    }
	    if (0 == kid_signal) {
		fprintf(stderr, "Signal '%s' not recognized.\n", optarg);
		exit(1);
	    }
	
	case 'k':
	    if (isalnum(optarg[0]))
	    {
		def_board  = getdigit(optarg[0]);
		def_module = isalnum(optarg[1]) ? getdigit(optarg[1]) : 0;
	    }
	    else
	    {
		def_board  = 0;
		def_module = 0;
	    }
	    break;

	case 'L':
#if HAVE_LIBDL
	    {
		void *handle;

		handle = dlopen(optarg, RTLD_NOW);
		if (!handle)
		{
		    fprintf(stderr, "%s\n", dlerror());
		    exit(1);
		}
	    }
#else
	    fprintf(stderr, "LIBDL not compiled in!\n");
#endif
	    break;

	case 'm':
#if HAVE_SOCKET
	    hostname = optarg;
	    use_gdb = 0;
	    break;
#else
	    fprintf(stderr, "Networking support not compiled in!\n");
	    goto usage;
#endif

	case 'M':			/* -M physaddr:len */
#if HAVE_MMAP
	    mempa = (off_t) strtoul(optarg, (char **) NULL, 0);
	    cp = strchr(optarg, ':');
	    if (!cp) goto usage;
	    memlen = strtoul(cp+1, (char **) NULL, 0);
	    break;
#else
	    fprintf(stderr, "MMAP not compiled in!\n");
	    goto usage;
#endif

	case 'n':
	    symparam = symname = optarg;
	    break;

	case 'p':
	    frow = atoi(optarg) - 1;
	    break;

	case 'P':
#if HAVE_PTRACE
	    pid = atoi(optarg);
#else
	    fprintf(stderr, "PTRACE not compiled in!\n");
	    return(2);
#endif
	    break;

	case 'r':
	    writeflag = 0;
	    break;

	case 's':
	    swapflag = !swapflag;
	    break;

	case 'S':
	    if (sscanf(optarg, "%lx%c", &addr_mask, &ch) != 1 ||
		addr_mask < 0x100
		)
		goto usage;

	    fprintf(stderr, "address mask = %lx\n", addr_mask);
	    break;

	case 't':
	    index_mode = 0;
	    break;

	case 'u':
	    uptime = atoi(optarg);
	    break;

	case 'U':
#if HAVE_SOCKET
	    gdb_port = optarg;
	    if (sscanf(optarg, "%d%c", &udp_port, &ch) != 1)
		goto usage;
	    break;
#else
	    fprintf(stderr, "Networking not compiled in!\n");
	    goto usage;
#endif

	case 'V':
	    version_info();
	    break;

	case 'w':
	    if (sscanf(optarg, "%d%c", &colwidth, &ch) != 1 ||
		colwidth < 8 || colwidth > 12
		)
		goto usage;
	    break;

	case 'x':
	    debug++;
	    break;

	default:
	    goto usage;
	}
    }

    /*
     *  Default symbol file name if none specified.
     */

    /*
     *	Get define file(s).
     */

    dl_fmt_cnt = fmt_cnt;

    if (!readdefs())
    {
	sleep(2);
    }

    /*
     *  Read in symbols.
     */

    if (symname == 0)
    {
#if HAVE_DEV_KSYMS
	symname = (strcmp(corename, "/dev/kmem") == 0 ?
		   "/dev/ksyms" : "kme_syms");
#elif HAVE_PROC_KSYMS
	symname = (strcmp(corename, "/dev/kmem") == 0 ?
		   "/proc/ksyms" : "kme_syms");
#else
	symname = "kme_syms";
#endif
    }
    
    if (!readsym() && strcmp(symname, "kme_syms"))
    {
	fprintf(stderr, "Problem with: %s\n", symname);
	perror(symname);
	sleep(2);
    }

    /*
     *  Now that we have symbols available, read in
     *  the screen configuration.
     */

    while (optind != argc)
    {
	if (!inparam(argv[optind++])) goto usage;
    }

    /*
     *	Open memory access file.
     */

    if (fepdev)
    {
	fepfd = open(fepdev, O_WRONLY|O_NDELAY);

	if (fepfd < 0)
	    fepfd = open(fepdev, O_RDONLY|O_NDELAY);

	if (fepfd < 0)
	{
	    fprintf(stderr, "Cannot open ");
	    perror(fepdev);
	    return(2);
	}
	
#if HAVE_STROPTS_H
	/* Test to see if this is a STREAM device */
	strdev = ! (ioctl(fepfd, I_FLUSH, FLUSHRW) == -1);
#endif
    }

#if HAVE_PTRACE
    else if (pid != 0)
    {
	int status;

	/* Attach to the child, then wait for him to stop */

	sigaction_alarm.sa_handler = catch_alarm;

	sigaction(SIGALRM, &sigaction_alarm, 0);

	if (ptrace(PTRACE_ATTACH, pid, 0, 0) != 0)
	{
	    perror("Cannot PTRACE_ATTACH process");
	    return 2;
	}

	sigalarm = 0;
	alarm(1);

	while (wait(&status) != pid && sigalarm == 0) {
	    printf("wait again\n");
	}
	    
	alarm(0);

	/* Arrange to catch all his signals */

	sigaction_child.sa_handler = watch_the_kid;
    
	sigaction(SIGCLD, &sigaction_child, 0);

	/* Start him up again */

	if (ptrace(PTRACE_CONT, pid, 0, 0) != 0)
	{
	    perror("Cannot PTRACE_CONT process");
	    return 2;
	}

	pidrun = 1;
    }
#endif

#if HAVE_SOCKET
    else if (hostname)
    {
	if (use_gdb)
	    open_gdb(hostname);	/* Use GDB protocol */
	else
	    opensoc(hostname);	/* Use KME protocol */
    }
#endif

    else if (elfcore) {
	    if (read_corefile(elfcore)) {
		fprintf(stderr, "Cannot open corefile '%s'\n", elfcore);
		goto usage;
	    }
    }

    else
    {
	if ((memfd = open(corename, writeflag ? O_RDWR : O_RDONLY)) == -1)
	{
	    perror(corename);
	    return(2);
	}
    }

#if HAVE_MMAP
    if (memlen)
    {
	memva = (uchar *) mmap((caddr_t) 0, memlen,
			       PROT_READ | (writeflag ? PROT_WRITE : 0),
			       MAP_SHARED, memfd, mempa);
	if (memva == (void *) -1)
	{
	    perror("can't mmap corefile");
	    return(2);
	}
    }
#endif

    /*
     *	Initialize curses.
     */

#if OLDCURSE
#if HAVE_TERMIOS_H
    tcgetattr(0, &otio);
#else
    ioctl(0, TCGETA, &otio);
#endif
#endif

    initscr();

#if OLDCURSE
#if HAVE_TERMIOS_H
    tcgetattr(0, &tio);
#else
    ioctl(0, TCGETA, &tio);
#endif
    tio.c_lflag &= ~(ECHO|ICANON);
    tio.c_iflag &= ~(ICRNL|IGNCR|INLCR);
#else
    noecho();
    typeahead(-1);
    keypad(stdscr, 1);
#endif

#if TERMINFO
    idlok(stdscr, idline);
#endif

    /*
     *	Figure number of rows/columns on screen.
     */

    nrow = LINES;
    if (nrow > NADR) nrow = NADR;
    nrow -= helpflag;

    scount = nrow / 2;

    ncol = (COLS - FCOL(1)) / colwidth;

    if (nrow * ncol > NDATA) ncol = NDATA / nrow;

    /*
     * Catch interrupts.
     */

    sigaction_int.sa_handler = catch_int;

    sigaction(SIGINT, &sigaction_int, 0);

    /*
     *	Run the main loop.
     */

    irow = frow;
    mainloop();

    /*
     *	Exit curses.
     */

    move(LINES-1, 0);
    refresh();

    endwin();

    putchar('\n');

#if HAVE_PTRACE
    /*
     *  Detach from controlled process.
     */

    if (quitreq >= 2)
	printf("Child exited\n");

    if (pid != 0)
    {
	stopchild();

	if (ptrace(PTRACE_DETACH, pid, 0, 0) != 0)
	    perror("PTRACE_DETACH");
    }
#endif

#if HAVE_SOCKET
    /*
     * Detach from GDB devices
     */
    if (nsoc && use_gdb)
    {
	int i;
	for (i = 0; i < nsoc; ++i)
	{
	    /* issue detach command */
	    gdb_detach(socfd[i]);
	    /* drain any input, keeps RedBoot from hanging */
	    gdb_flushin(socfd[i]);
	}
    }
#endif

    return(0);

 usage:
    fprintf(stderr,
	    "usage: %s [-ahirstVx] [-A addrscale] [-b base] [-c core]\n"
	    "\t[-d defs] [-D defspath] [-f tty] [-g host:port,...]\n"
	    "\t[-I signal] [-k node] [-L lib] [-m hostlist] [-M addr:len]\n"
	    "\t[-n symfile] [-p row] [-u uptime] [-Mstartaddr:size]\n"
	    "\t[-S address mask] [-P pid] [-S mask] [-u uptime]\n"
	    "\t[-U port] [-w colwidth] [param ...]\n",
	    argv[0]);
    return(2);
}
