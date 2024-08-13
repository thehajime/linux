/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/utsname.h>
#include <linux/syscalls.h>
#include <asm/current.h>
#include <asm/mman.h>
#include <linux/uaccess.h>
#include <asm/unistd.h>

/*
 * XXX: copied from musl dynlink.c
 * musl has a switch, DL_NOMMU_SUPPORT, to support nommu kernel, but it's
 * a compilation-time switch, which isn't enabled on x86_64 platform.  so
 * emulate the behavior of DL_NOMMU_SUPPORT here, until musl support it over
 * a runtime-switch.
 */

static void *mmap_fixed(void *p, size_t n, int prot, int flags, int fd, off_t off)
{
	char *q;
	ssize_t r;

	/* Fallbacks for MAP_FIXED failure on NOMMU kernels. */
	if (flags & MAP_ANONYMOUS) {
		memset(p, 0, n);
		return p;
	}
	if (ksys_lseek(fd, off, SEEK_SET) < 0) return -1;
	for (q=p; n; q+=r, off+=r, n-=r) {
		r = ksys_read(fd, q, n);
		if (r < 0 && r != -EINTR) return -1;
		if (!r) {
			memset(q, 0, n);
			break;
		}
	}
	return p;
}

long old_mmap(unsigned long addr, unsigned long len,
	      unsigned long prot, unsigned long flags,
	      unsigned long fd, unsigned long offset)
{
	long err = -EINVAL;
	if (offset & ~PAGE_MASK) {
		//printk(KERN_DEBUG "*************************************************************\n");
		//printk(KERN_DEBUG "**old_mmap should exit when offset is not alined to a page.**\n");
		//printk(KERN_DEBUG "**we are not! for some reason musl libc is sending us this.**\n");
		//printk(KERN_DEBUG "*************************************************************\n");
		offset *= 4096;
	}
#ifndef CONFIG_MMU
	if (flags & MAP_FIXED)
		return mmap_fixed(addr, len, prot, flags, fd, offset);
#endif

	err = ksys_mmap_pgoff(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
 out:
	return err;
}

SYSCALL_DEFINE3(mprotect, unsigned long, start, size_t, len,
		unsigned long, prot)
{
	return 0;
}
