// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>


char lkl_um_devs[4096];

/* from sigio.c */
void maybe_sigio_broken(int fd, int read)
{
}

/* from process.c */
int os_getpid(void)
{
	return getpid();
}


/* from chan_kern.c */
void free_irqs(void)
{
}

/* from sigio.c */
int ignore_sigio_fd(int fd)
{
	return 0;
}

/* from util.c */
/*
 * UML helper threads must not handle SIGWINCH/INT/TERM
 */
void os_fix_helper_signals(void)
{
	signal(SIGWINCH, SIG_IGN);
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
}



void os_kill_process(int pid, int reap_child)
{
	kill(pid, SIGKILL);
	if (reap_child) {
		while ((errno = 0, ((waitpid(pid, NULL, __WALL)) < 0))
		       && (errno == EINTR))
			;
	}

}
