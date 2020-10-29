/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LKL_H
#define _LKL_H

#ifdef __cplusplus
extern "C" {
#endif

#define _LKL_LIBC_COMPAT_H

#ifdef __cplusplus
#define class __lkl__class
#endif

/*
 * Avoid collisions between Android which defines __unused and
 * linux/icmp.h which uses __unused as a structure field.
 */
#pragma push_macro("__unused")
#undef __unused

#include <lkl/asm/syscalls.h>

#pragma pop_macro("__unused")

#ifdef __cplusplus
#undef class
#endif

/**
 * lkl_printf - print a message via the host print operation
 *
 * @fmt: printf like format string
 */
int lkl_printf(const char *fmt, ...);

/**
 * lkl_strerror - returns a string describing the given error code
 *
 * @err - error code
 * @returns - string for the given error code
 */
const char *lkl_strerror(int err);

/**
 * lkl_perror - prints a string describing the given error code
 *
 * @msg - prefix for the error message
 * @err - error code
 */
void lkl_perror(char *msg, int err);

#if __LKL__BITS_PER_LONG == 64
#define lkl_sys_fstatat lkl_sys_newfstatat
#define lkl_sys_fstat lkl_sys_newfstat

#else
#define __lkl__NR_fcntl __lkl__NR_fcntl64

#define lkl_stat lkl_stat64
#define lkl_sys_stat lkl_sys_stat64
#define lkl_sys_lstat lkl_sys_lstat64
#define lkl_sys_truncate lkl_sys_truncate64
#define lkl_sys_ftruncate lkl_sys_ftruncate64
#define lkl_sys_sendfile lkl_sys_sendfile64
#define lkl_sys_fstatat lkl_sys_fstatat64
#define lkl_sys_fstat lkl_sys_fstat64
#define lkl_sys_fcntl lkl_sys_fcntl64

#define lkl_statfs lkl_statfs64

static inline int lkl_sys_statfs(const char *path, struct lkl_statfs *buf)
{
	return lkl_sys_statfs64(path, sizeof(*buf), buf);
}

static inline int lkl_sys_fstatfs(unsigned int fd, struct lkl_statfs *buf)
{
	return lkl_sys_fstatfs64(fd, sizeof(*buf), buf);
}

#define lkl_sys_nanosleep lkl_sys_nanosleep_time32
static inline int lkl_sys_nanosleep_time32(struct lkl_timespec *rqtp,
					   struct lkl_timespec *rmtp)
{
	long p[6] = {(long)rqtp, (long)rmtp, 0, 0, 0, 0};

	return lkl_syscall(__lkl__NR_nanosleep, p);
}

#endif

static inline int lkl_sys_stat(const char *path, struct lkl_stat *buf)
{
	return lkl_sys_fstatat(LKL_AT_FDCWD, path, buf, 0);
}

static inline int lkl_sys_lstat(const char *path, struct lkl_stat *buf)
{
	return lkl_sys_fstatat(LKL_AT_FDCWD, path, buf,
			       LKL_AT_SYMLINK_NOFOLLOW);
}

#ifdef __lkl__NR_openat
/**
 * lkl_sys_open - wrapper for lkl_sys_openat
 */
static inline long lkl_sys_open(const char *file, int flags, int mode)
{
	return lkl_sys_openat(LKL_AT_FDCWD, file, flags, mode);
}

/**
 * lkl_sys_creat - wrapper for lkl_sys_openat
 */
static inline long lkl_sys_creat(const char *file, int mode)
{
	return lkl_sys_openat(LKL_AT_FDCWD, file,
			      LKL_O_CREAT|LKL_O_WRONLY|LKL_O_TRUNC, mode);
}
#endif

#ifdef __lkl__NR_mkdirat
/**
 * lkl_sys_mkdir - wrapper for lkl_sys_mkdirat
 */
static inline long lkl_sys_mkdir(const char *path, mode_t mode)
{
	return lkl_sys_mkdirat(LKL_AT_FDCWD, path, mode);
}
#endif

#ifdef __lkl__NR_epoll_create1
/**
 * lkl_sys_epoll_create - wrapper for lkl_sys_epoll_create1
 */
static inline long lkl_sys_epoll_create(int size)
{
	return lkl_sys_epoll_create1(0);
}
#endif

#ifdef __lkl__NR_epoll_pwait
/**
 * lkl_sys_epoll_wait - wrapper for lkl_sys_epoll_pwait
 */
static inline long lkl_sys_epoll_wait(int fd, struct lkl_epoll_event *ev,
				      int cnt, int to)
{
	return lkl_sys_epoll_pwait(fd, ev, cnt, to, 0, _LKL_NSIG/8);
}
#endif

#ifdef __cplusplus
}
#endif

#endif
