/*
 *          Kernel Memory Editor (KME) - map signals names to signal numbers.
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


#include <signal.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "signalname.h"

#if !defined (NSIG)
#  define NSIG 100
#endif

/*
 * Try to provide a mapping between commonly known names ("SIGSTP") and
 * signal numbers (23) so that developers don't have to know them and 
 * yet doesn't codify constants.  Names can be added in the same style
 * with impunity.
 * 
 * This ends up sort of being a reverse but using the "programmer" names 
 * instead of the human-readable ones.
 */

static char *signal_names[NSIG];

void
init_signal_names(void)
{
#if defined (SIGHUP)
    signal_names[SIGHUP] = "SIGHUP";
#endif

#if defined (SIGINT)
    signal_names[SIGINT] = "SIGINT";
#endif

#if defined (SIGQUIT)
    signal_names[SIGQUIT] = "SIGQUIT";
#endif

#if defined (SIGILL)
    signal_names[SIGILL] = "SIGILL";
#endif

#if defined (SIGTRAP)
    signal_names[SIGTRAP] = "SIGTRAP";
#endif

#if defined (SIGABRT)
    signal_names[SIGABRT] = "SIGABRT";
#endif

#if defined (SIGBUS)
    signal_names[SIGBUS] = "SIGBUS";
#endif

#if defined (SIGFPE)
    signal_names[SIGFPE] = "SIGFPE";
#endif

#if defined (SIGKILL)
    signal_names[SIGKILL] = "SIGKILL";
#endif

#if defined (SIGUSR1)
    signal_names[SIGUSR1] = "SIGUSR1";
#endif

#if defined (SIGSEGV)
    signal_names[SIGSEGV] = "SIGSEGV";
#endif

#if defined (SIGUSR2)
    signal_names[SIGUSR2] = "SIGUSR2";
#endif

#if defined (SIGPIPE)
    signal_names[SIGPIPE] = "SIGPIPE";
#endif

#if defined (SIGALRM)
    signal_names[SIGALRM] = "SIGALRM";
#endif

#if defined (SIGTERM)
    signal_names[SIGTERM] = "SIGTERM";
#endif

#if defined (SIGCHLD)
    signal_names[SIGCHLD] = "SIGCHLD";
#endif

#if defined (SIGCONT)
    signal_names[SIGCONT] = "SIGCONT";
#endif

#if defined (SIGSTOP)
    signal_names[SIGSTOP] = "SIGSTOP";
#endif

#if defined (SIGTSTP)
    signal_names[SIGTSTP] = "SIGTSTP";
#endif

#if defined (SIGTTIN)
    signal_names[SIGTTIN] = "SIGTTIN";
#endif

#if defined (SIGTTOU)
    signal_names[SIGTTOU] = "SIGTTOU";
#endif

#if defined (SIGURG)
    signal_names[SIGURG] = "SIGURG";
#endif

#if defined (SIGXCPU)
    signal_names[SIGXCPU] = "SIGXCPU";
#endif

#if defined (SIGXFSZ)
    signal_names[SIGXFSZ] = "SIGXFSZ";
#endif

#if defined (SIGVTALRM)
    signal_names[SIGVTALRM] = "SIGVTALRM";
#endif

#if defined (SIGPROF)
    signal_names[SIGPROF] = "SIGPROF";
#endif

#if defined (SIGWINCH)
    signal_names[SIGWINCH] = "SIGWINCH";
#endif

#if defined (SIGIO)
    signal_names[SIGIO] = "SIGIO";
#endif

#if defined (SIGPWR)
    signal_names[SIGPWR] = "SIGPWR";
#endif

#if defined (SIGSYS)
    signal_names[SIGSYS] = "SIGSYS";
#endif

#if defined (SIGRTMIN)
    signal_names[SIGRTMIN] = "SIGRTMIN";
#endif
}

/*
 * Simple sequential search.  Returns signal number or -1 if not found.
 */
int
find_signal_name(const char *nm)
{
	unsigned i;
	for (i = 0; i < sizeof signal_names / sizeof(signal_names[0]); i++) {
		if (signal_names[i] && (0 == strcmp(nm, signal_names[i]))) {
			return i;
		}
	}
	return -1;
}

