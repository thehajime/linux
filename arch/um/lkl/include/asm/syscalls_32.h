/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_SYSCALLS_32_H
#define __UM_LIBMODE_SYSCALLS_32_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/signal.h>

#if __BITS_PER_LONG == 32

/* kernel/syscalls_32.c */

#ifdef CONFIG_MMU
struct mmap_arg_struct32;
asmlinkage long sys32_mmap(struct mmap_arg_struct32 __user *);
#endif

asmlinkage long sys32_pread64(unsigned int, char __user *, u32, u32, u32);
asmlinkage long sys32_pwrite64(unsigned int, const char __user *, u32, u32, u32);

long sys32_fadvise64_64(int a1, __u32 a2, __u32 a3, __u32 a4, __u32 a5, int a6);

asmlinkage ssize_t sys32_readahead(int, unsigned int, unsigned int, size_t);
asmlinkage long sys32_sync_file_range(int, unsigned int, unsigned int,
				      unsigned int, unsigned int, unsigned int);
asmlinkage long sys32_sync_file_range2(int, unsigned int,
				       unsigned int, unsigned int,
				       unsigned int, unsigned int);
asmlinkage long sys32_fadvise64(int, unsigned int, unsigned int, size_t, int);
asmlinkage long sys32_fallocate(int, int, unsigned int,
				unsigned int, unsigned int, unsigned int);

#endif /* __BITS_PER_LONG */

#endif
