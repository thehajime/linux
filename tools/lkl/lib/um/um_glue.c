// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>



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
