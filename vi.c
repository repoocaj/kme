/*
 * All source code originally from the "SC" Spreadsheet Calculator.
 *
 * 	original by James Gosling, September 1982
 * 	modified by Mark Weiser and Bruce Israel,
 * 		University of Maryland
 * 	R. Bond  12/86
 * 	More mods by Alan Silverstein, 3-4/88
 * 	$Revision: 1.6 $
 *
 * Severely hacked for inclusion into KME by Gene Olson.
 *
 * Original source code is public domain and is widely
 * available on the internet.  See the kme AUTHORS file
 * for a full list of contributors.
 *
 * This modified version is released under the GNU General
 * Public License.
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

char vi_version[] = "@(#)vi.c $Revision: 1.6 $ $Date: 2004/12/12 21:20:30 $" ;

#include "config.h"

#include <sys/types.h>

#if HAVE_LIBNCURSES 
#  include <ncurses.h>
#endif

#if HAVE_LIBCURSES 
#  include <curses.h>
#endif

#if HAVE_STRINGS_H
# include <strings.h>
#endif

#ifdef HAVE_STRING_H
# include <string.h>
#endif

#include <signal.h>

extern int readch();

#include <stdio.h>
#include <ctype.h>

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif
 
#if HAVE_STRING_H
#  include <string.h>
#endif

/****** Definitions from the sc.h header file ******/

#define HISTLEN  25	/* Number of history entries for vi emulation */

#define ctl(c) ((c)&037)
#define ESC 033

/********** SC memory allocation routines *******************/

char *
xrealloc(char* ptr, unsigned n)
{
    ptr = ptr ? realloc(ptr, n) : malloc(n) ;

    if (ptr == 0)
    {
	(void) fprintf(stderr, "Out of memory\n") ;
	exit(1) ;
    }
    return(ptr) ;
}


/*********** Remaining text **mostly** from vi.c ************/

#define istext(a) (isalnum(a) || ((a) == '_'))
#define isother(a) (!isspace(a) && !istext(a))

void save_hist();

static void append_line();
static void back_hist();
static int  back_char();
static int  back_word();
static void back_space();
static void kill_insert();
static void change_cmd();
static void col_0();
static void cr_line();
static void delete_cmd();
static void del_chars();
static void del_in_line();
static void dotcmd();
static int  find_char();
static void for_hist();
static int  for_char();
static int  for_word();
static void ins_in_line();
static void rep_char();
static void replace_in_line();
static void replace_mode();
static void restore_it();
static void savedot();
static void search_again();
static void search_hist();
static void search_mode();
static void stop_edit();
static void put_in_line();
static void u_save();
static int motion(int);
static void edit_mode();

void insert_mode();

#define LINELEN  200

int (*lineproc)() ;		/* Callback procedure */

char line[LINELEN+4] ;		/* Line buffer */
int linepos ;			/* Line position */
int insertpos ;			/* Line position at beginning of insert */

int mode_ind;			/* Mode indicator */

/* values for mode below */

#define INSERT_MODE	0	/* Insert mode */
#define EDIT_MODE       1	/* Edit mode */
#define REP_MODE        2	/* Replace mode */
#define SEARCH_MODE	3	/* Get arguments for '/' command */

#define	DOTLEN		200

static int mode = INSERT_MODE;
char *history[HISTLEN+1] ;

static int count;
static int histp ;
static int histin ;
static int histout ;
static char last_search[LINELEN+1] ;
static char undo_line[LINELEN+1] ;
static char putbuf[LINELEN+1] ;
static int undo_pos = -1;
static char dotb[DOTLEN];
static int dotcnt ;
static int doti ;
static int do_dot ;

void
update_line(int c)
{
    switch(mode)
    {
    case EDIT_MODE:
	if (isdigit(c)) {
	    if (c == '0' && count == 0) col_0();
	    else count = 10*count + (c - '0');
	    return;
	}
	if (count == 0) count = 1;

	switch(c)
	{
	case ctl('h'):	linepos = back_char();			break;
	case ctl('j'):
	case ctl('m'):	cr_line();				break;
	case ESC:	stop_edit();				break;
	case ' ':	linepos = for_char();			break;
#ifdef KEY_LEFT
	case KEY_LEFT:
#endif
#ifdef KEY_BACKSPACE
	case KEY_BACKSPACE:
#endif	    
#ifdef KEY_RIGHT
	case KEY_RIGHT:
#endif
	case '$':
	case 'b':
	case 'f':
	case 'h':
	case 'l':
	case 't':
	case 'w':	linepos = motion(c);			break;
	case '+':	for_hist();				break;
	case '-':	back_hist();				break;
	case '.':	dotcmd();				break;
	case '/':	search_mode();				break;
	case 'a':	u_save(c); append_line();		break;
	case 'c':	u_save(c); change_cmd();		break;
	case 'C':	u_save(c); line[linepos]=0;
			insert_mode();				break;
	case 'D':	u_save(c); line[linepos]=0;		break;
	case 'd':	u_save(c); delete_cmd();		break;
	case 'I':	u_save(c); col_0(); insert_mode();	break;
	case 'i':	u_save(c); insert_mode();		break;
#ifdef KEY_DOWN
	case KEY_DOWN:
#endif	    
	case 'j':	for_hist();				break;
#ifdef KEY_UP
	case KEY_UP:
#endif	    
	case 'k':	back_hist();				break;
	case 'n':	search_again();				break;
	case 'p':	linepos++;
	case 'P':	put_in_line();				break;
	case 'q':	stop_edit();				break;
	case 'R':	u_save(c); replace_mode();		break;
	case 'r':	u_save(c); rep_char();			break;
	case 's':	u_save(c); del_in_line();
			insert_mode() ;				break;
	case 'u':	restore_it();				break;
	case 'X':	u_save(c); back_space();		break;
	case 'x':	u_save(c); del_in_line();		break;
	default:	beep();					break;
	}
	break;
    
    case INSERT_MODE:
	savedot(c);
	switch(c) {
#ifdef KEY_BACKSPACE
	case KEY_BACKSPACE:
#endif	    
#ifdef KEY_LEFT
	case KEY_LEFT:
#endif	    
	case ctl('h'):	back_space();				break;
#ifdef KEY_UP
	case KEY_UP:
#endif
#ifdef KEY_DOWN
	case KEY_DOWN:
#endif	    
#ifdef KEY_RIGHT
	case KEY_RIGHT:	beep();					break;
#endif
	case ctl('j'):
	case ctl('m'):	cr_line();				break;
	case ctl('u'):	kill_insert();/* TODO: use kill char */	break;
	case ESC:	edit_mode();				break;
	default:	ins_in_line(c);				break;
	}
	break;
    
    case SEARCH_MODE:
	switch(c) {
	case ctl('h'):	back_space();				break;
	case ctl('j'):
	case ctl('m'):	search_hist();				break;
	case ESC:	edit_mode();				break;
	default:	ins_in_line(c);				break;
	}
	break;

    case REP_MODE:
	savedot(c);
	switch(c) {
	case ctl('h'):	back_space();				break;
	case ctl('j'):
	case ctl('m'):	cr_line();				break;
	case ESC:	edit_mode();				break;
	default:	replace_in_line(c);			break;
	}
	break;
    }

    count = 0;

    if (mode == EDIT_MODE && linepos > 0 && linepos >= (int)strlen(line))
	linepos = strlen(line) - 1;
}

void edit_mode()
{
    if (mode == INSERT_MODE && linepos > 0) linepos--;
    mode = EDIT_MODE;
    mode_ind = ' ';
    histp = histin ;
}

void insert_mode()
{
    mode_ind = '_';
    mode = INSERT_MODE;
    insertpos = linepos;
}

static	void
search_mode()
{
    line[0] = '/';
    line[1] = '\0';
    linepos = 1;
    histp = histin ;
    mode_ind = ' ';
    mode = SEARCH_MODE;
}

static	void
replace_mode()
{
    mode_ind = '_';
    mode = REP_MODE;
}

/* dot command functions.  Saves info so we can redo on a '.' command */

static	void
savedot(int c)
{
    if (do_dot || (c == '\n'))
	return;

    if (doti < DOTLEN-1)
    {
	dotb[doti++] = c;
	dotb[doti] = '\0';
    }
}

static int dotcalled = 0;

static	void
dotcmd()
{
    int c;

    if (dotcalled)	/* stop recursive calling of dotcmd() */
	return;
    do_dot = 1;
    doti = 0;
    count = dotcnt;
    while(dotb[doti] != '\0') {
	c = dotb[doti++];
	dotcalled = 1;
	update_line(c);
    }
    do_dot = 0;
    doti = 0;
    dotcalled = 0;
}

int
vigetch()
{
    int c;

    if (do_dot) {
	if (dotb[doti] != '\0') {
	    return(dotb[doti++]);
	} else {
	    do_dot = 0;
	    doti = 0;
	    return(readch());
	}
    }
    c = readch();
    savedot(c);
    return(c);
}

/* saves the current line for possible use by an undo cmd */
static	void
u_save(int c)
{
    histp = histin ;

    (void) strcpy(undo_line, line);

    undo_pos = linepos;

    /* reset dot command if not processing it. */

    if (!do_dot) {
	dotcnt = count;
        doti = 0;
	savedot(c);
    }
}

/* Restores the current line saved by u_save() */
static	void
restore_it()
{
    register int tempi ;
    static char tempc[LINELEN+1] ;

    if (undo_pos < 0)
    {
	beep();
	return;
    }

    histp = histin ;

    tempi = linepos ;
    (void) strcpy(tempc, line) ;

    linepos = undo_pos ;
    (void) strcpy(line, undo_line) ;

    undo_pos = tempi ;
    (void) strcpy(undo_line, tempc) ;
}

/* This command stops the editing process. */
static	void
stop_edit()
{
    linepos = -1;
}

/*
 * Motion commands.  Forward motion commands take an argument
 * which, when set, cause the forward motion to continue onto
 * the null at the end of the line instead of stopping at the
 * the last character of the line.
 */
static	int
for_char()
{
    register int i ;

    if (line[linepos] == 0) return(-1);

    for (i = linepos ;;) {
	if (line[++i] == 0) return(i);
	if (--count <= 0) return(i);
    }
}

static	int
for_word()
{
    register int i;

    for (i = linepos ;;) {
	if (istext(line[i])) {
	    while (++i, istext(line[i])) ;
	} else if (line[i] && !isspace(line[i])) {
	    while (++i, line[i] && !isspace(line[i]) && !istext(line[i]));
	}
	while (isspace(line[i])) i++ ;
	if (--count <= 0) return(i);
	else if (line[i] == 0) return(-1);
    }
}

static	int
back_char()
{
    register int i ;

    if (linepos == 0) return(-1) ;

    i = linepos - (count ? count : 1);
    if (i < 0) i = 0 ;
    return(i);
}

static	int
back_word()
{
    register int i;

    for (i = linepos;;) {
	for (;;) {
	    if (--i < 0) return(i);
	    if (!isspace(line[i])) break;
	}
	if (istext(line[i])) {
	    while (i && istext(line[i-1])) i--;
	} else {
	    while (i && !isspace(line[i-1]) && !istext(line[i-1])) i--;
	}
	if (--count <= 0) return(i);
    }
}

/* Text manipulation commands */

static	void
del_in_line()
{
    register int s ;
    register int d ;
    register int p ;

    s = d = linepos ;
    p = 0 ;

    while (line[s]) {
	if (--count >= 0)
	    putbuf[p++] = line[s++] ;
	else
	    line[d++] = line[s++] ;
    }

    putbuf[p] = 0 ;
    line[d] = 0 ;
}

static void
put_in_line()
{
    register int p ;
    register int n ;

    p = strlen(putbuf) ;
    n = strlen(line) ;
    if (p + n >= LINELEN) {
	beep() ;
	return ;
    }

    for (; n >= linepos ; --n) line[n+p] = line[n] ;

    for (n = p ; --n >= 0 ;) line[linepos+n] = putbuf[n] ;

    linepos += p - 1 ;
}

static	void
ins_in_line(int c)
{
    register int i, len;

    len = strlen(line);
    if (len >= LINELEN) {
	beep();
	return;
    }
    for (i = len; i >= linepos; --i)
	line[i+1] = line[i];
    line[linepos++] = c;
}

static	void
append_line()
{
    if (line[linepos]) linepos++;
    insert_mode();
}

static	void
rep_char()
{
    if (line[linepos] != '\0') {
    	line[linepos] = vigetch();
    } else {
	line[linepos] = vigetch();
	line[linepos+1] = '\0';
    }
}

static	void
replace_in_line(int c)
{
    if (line[linepos]) line[linepos++] = c ;
    else if (linepos >= LINELEN) beep() ;
    else {
	line[linepos++] = c ;
	line[linepos] = 0 ;
    }
}

static	void
back_space()
{
    int pos;

    pos = linepos - (count ? count : 1);
    if (pos < 0) pos = 0 ;

    del_chars(pos, linepos);
}

static void kill_insert()
{
    int pos;

    pos = insertpos;
    if (pos < 0) pos = 0 ;

    del_chars(pos, linepos);
}

int get_motion()
{
    return(motion(vigetch()));
}

int motion(int c)
{
    register int cpos;

    switch (c) {
    case 'b':  cpos = back_word();		break;
    case 'f':  cpos = find_char();		break;
#ifdef KEY_LEFT
    case KEY_LEFT:
#endif
#ifdef KEY_BACKSPACE
    case KEY_BACKSPACE:
#endif
    case 'h':  cpos = back_char();		break;
#ifdef KEY_RIGHT
    case KEY_RIGHT:
#endif
    case 'l':  cpos = for_char();		break;
    case 't':  cpos = find_char()-1;		break;
    case 'w':  cpos = for_word();		break;
    case '$':  cpos = strlen(line);		break;
    default:   cpos = -1;
    }

    if (cpos >= 0) return(cpos) ;

    beep();
    return(linepos);
}


static	void
delete_cmd()
{
    int cpos;

    cpos = get_motion();
    if (cpos < 0) beep();
    else del_chars(cpos, linepos);
}

static	void
change_cmd()
{
    delete_cmd();
    insert_mode();
}

static	void
del_chars(int first, int last)
{
    if (first < last) {
	linepos = first;
	count = last - first ;
	del_in_line();
    }
    else if (first > last) {
	linepos = last;
	count = first - last ;
	del_in_line();
    }
}

static	void
cr_line()
{
    save_hist();
    if ((*lineproc)(line)) linepos = -1 ;
    else beep() ;
}

/* History functions */

void
save_hist()
{
    if  (   line[0] == 0
	||  (   histin != histout
	    &&  strcmp(history[histin ? histin - 1 : HISTLEN], line) == 0
	    )
	)
	return ;

    history[histin] = xrealloc(history[histin], strlen(line) + 1) ;
    (void) strcpy(history[histin], line);

    histp = histin ;
    linepos = 0 ;

    if (++histin > HISTLEN) histin = 0 ;
    if (histout == histin && ++histout > HISTLEN) histout = 0 ;
}


static	void
back_hist()
{
    if (histp == histout) {
	beep() ;
	return ;
    }

    if (histp == histin) save_hist() ;

    if (--histp < 0) histp = HISTLEN - 1 ;

    (void) strcpy(line, history[histp]);
    linepos = 0 ;
}

static	void
search_hist()
{
    (void) strcpy(last_search, line+1) ;

    search_again() ;

    mode = EDIT_MODE;
}


char *
strsearch(char* s1, char* s2)
{
    register char *p1 ;
    register char *p2 ;

    for (; *s1 ; s1++)
    {
	for (p1 = s1, p2 = s2 ;; p1++, p2++)
	{
	    if (*p2 == 0) return(s1) ;

	    if (*p1 != *p2) break ;
	}
    }

    return(0) ;
}


static	void
search_again(void)
{
    int i;

    for (i = histp ;;) {
	if (i == histout) {
	    beep() ;
	    return ;
	}

	if (--i < 0) i = HISTLEN ;

	if (strsearch(history[i], last_search)) {
	    histp = i ;
	    strcpy(line, history[histp]) ;
	    linepos = 0 ;
	    return ;
	}
    }
}

static	void
for_hist(void)
{
    int i ;

    if (histp == histin || strcmp(history[histp], line)) {
	beep() ;
	return ;
    }

    i = histp ;
    if (++i > HISTLEN) i = 0 ;
    if (i == histin) {
	beep() ;
	return ;
    }
    histp = i ;

    strcpy(line, history[histp]) ;
    linepos = 0 ;
}

static	void
col_0(void)
{
    linepos = 0;
}

static	int
find_char(void)
{
    register int c;
    register int i;

    c = vigetch();

    for (i = linepos ;;)
    {
	if (line[i] == 0) return(-1);
	if (line[++i] == c && --count <= 0) return(i);
    }
}
