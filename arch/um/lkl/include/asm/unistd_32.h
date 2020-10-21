/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_UNISTD_32_H
#define __UM_LIBMODE_UNISTD_32_H

#include <asm/bitsperlong.h>

#ifndef __SYSCALL
#define __SYSCALL(x, y)
#endif

#if __BITS_PER_LONG == 32

#ifdef CONFIG_MMU
__SYSCALL(__NR3264_mmap, sys32_mmap)
#endif

__SYSCALL(__NR_pread64, sys32_pread64)
__SYSCALL(__NR_pwrite64, sys32_pwrite64)

__SYSCALL(__NR_readahead, sys32_readahead)
#ifdef __ARCH_WANT_SYNC_FILE_RANGE2
__SYSCALL(__NR_sync_file_range2, sys32_sync_file_range2)
#else
__SYSCALL(__NR_sync_file_range, sys32_sync_file_range)
#endif
/* mm/fadvise.c */
__SYSCALL(__NR3264_fadvise64, sys32_fadvise64_64)
__SYSCALL(__NR_fallocate, sys32_fallocate)

#endif

#endif
