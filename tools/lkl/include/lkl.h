/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LKL_H
#define _LKL_H

#include "lkl_autoconf.h"

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

#if defined(__MINGW32__)
#define strtok_r strtok_s
#define inet_pton lkl_inet_pton

int inet_pton(int af, const char *src, void *dst);
#endif

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
static inline int lkl_sys_nanosleep_time32(struct __lkl__kernel_timespec *rqtp,
					   struct __lkl__kernel_timespec *rmtp)
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

#ifdef __lkl__NR_llseek
/**
 * lkl_sys_lseek - wrapper for lkl_sys_llseek
 */
static inline long long lkl_sys_lseek(unsigned int fd, __lkl__kernel_loff_t off,
				      unsigned int whence)
{
	long long res;
	long ret = lkl_sys_llseek(fd, off >> 32, off & 0xffffffff, &res,
				  whence);

	return ret < 0 ? ret : res;
}
#endif

static inline void *lkl_sys_mmap(void *addr, size_t length, int prot, int flags,
				 int fd, off_t offset)
{
	return (void *)lkl_sys_mmap_pgoff((long)addr, length, prot, flags, fd,
					  offset >> 12);
}

#define lkl_sys_mmap2 lkl_sys_mmap_pgoff

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


#ifdef __lkl__NR_faccessat
/**
 * lkl_sys_access - wrapper for lkl_sys_faccessat
 */
static inline long lkl_sys_access(const char *file, int mode)
{
	return lkl_sys_faccessat(LKL_AT_FDCWD, file, mode);
}
#endif

#ifdef __lkl__NR_fchownat
/**
 * lkl_sys_chown - wrapper for lkl_sys_fchownat
 */
static inline long lkl_sys_chown(const char *path, lkl_uid_t uid, lkl_gid_t gid)
{
	return lkl_sys_fchownat(LKL_AT_FDCWD, path, uid, gid, 0);
}
#endif

#ifdef __lkl__NR_fchmodat
/**
 * lkl_sys_chmod - wrapper for lkl_sys_fchmodat
 */
static inline long lkl_sys_chmod(const char *path, mode_t mode)
{
	return lkl_sys_fchmodat(LKL_AT_FDCWD, path, mode);
}
#endif

#ifdef __lkl__NR_linkat
/**
 * lkl_sys_link - wrapper for lkl_sys_linkat
 */
static inline long lkl_sys_link(const char *existing, const char *new)
{
	return lkl_sys_linkat(LKL_AT_FDCWD, existing, LKL_AT_FDCWD, new, 0);
}
#endif

#ifdef __lkl__NR_unlinkat
/**
 * lkl_sys_unlink - wrapper for lkl_sys_unlinkat
 */
static inline long lkl_sys_unlink(const char *path)
{
	return lkl_sys_unlinkat(LKL_AT_FDCWD, path, 0);
}
#endif

#ifdef __lkl__NR_symlinkat
/**
 * lkl_sys_symlink - wrapper for lkl_sys_symlinkat
 */
static inline long lkl_sys_symlink(const char *existing, const char *new)
{
	return lkl_sys_symlinkat(existing, LKL_AT_FDCWD, new);
}
#endif

#ifdef __lkl__NR_readlinkat
/**
 * lkl_sys_readlink - wrapper for lkl_sys_readlinkat
 */
static inline long lkl_sys_readlink(const char *path, char *buf, size_t bufsize)
{
	return lkl_sys_readlinkat(LKL_AT_FDCWD, path, buf, bufsize);
}
#endif

#ifdef __lkl__NR_renameat
/**
 * lkl_sys_rename - wrapper for lkl_sys_renameat
 */
static inline long lkl_sys_rename(const char *old, const char *new)
{
	return lkl_sys_renameat(LKL_AT_FDCWD, old, LKL_AT_FDCWD, new);
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

#ifdef __lkl__NR_unlinkat
/**
 * lkl_sys_rmdir - wrapper for lkl_sys_unlinkrat
 */
static inline long lkl_sys_rmdir(const char *path)
{
	return lkl_sys_unlinkat(LKL_AT_FDCWD, path, LKL_AT_REMOVEDIR);
}
#endif

#ifdef __lkl__NR_mknodat
/**
 * lkl_sys_mknod - wrapper for lkl_sys_mknodat
 */
static inline long lkl_sys_mknod(const char *path, mode_t mode, dev_t dev)
{
	return lkl_sys_mknodat(LKL_AT_FDCWD, path, mode, dev);
}
#endif

#ifdef __lkl__NR_pipe2
/**
 * lkl_sys_pipe - wrapper for lkl_sys_pipe2
 */
static inline long lkl_sys_pipe(int fd[2])
{
	return lkl_sys_pipe2(fd, 0);
}
#endif

#ifdef __lkl__NR_sendto
/**
 * lkl_sys_send - wrapper for lkl_sys_sendto
 */
static inline long lkl_sys_send(int fd, void *buf, size_t len, int flags)
{
	return lkl_sys_sendto(fd, buf, len, flags, 0, 0);
}
#endif

#ifdef __lkl__NR_recvfrom
/**
 * lkl_sys_recv - wrapper for lkl_sys_recvfrom
 */
static inline long lkl_sys_recv(int fd, void *buf, size_t len, int flags)
{
	return lkl_sys_recvfrom(fd, buf, len, flags, 0, 0);
}
#endif

#ifdef __lkl__NR_pselect6
/**
 * lkl_sys_select - wrapper for lkl_sys_pselect
 */
static inline long lkl_sys_select(int n, lkl_fd_set *rfds, lkl_fd_set *wfds,
				  lkl_fd_set *efds, struct lkl_timeval *tv)
{
	long data[2] = { 0, _LKL_NSIG/8 };
	struct lkl_timespec ts;
	lkl_time_t extra_secs;
	const lkl_time_t max_time = ((1ULL<<8)*sizeof(time_t)-1)-1;

	if (tv) {
		if (tv->tv_sec < 0 || tv->tv_usec < 0)
			return -LKL_EINVAL;

		extra_secs = tv->tv_usec / 1000000;
		ts.tv_nsec = tv->tv_usec % 1000000 * 1000;
		ts.tv_sec = extra_secs > max_time - tv->tv_sec ?
			max_time : tv->tv_sec + extra_secs;
	}
	return lkl_sys_pselect6(n, rfds, wfds, efds, tv ?
				(struct __lkl__kernel_timespec *)&ts : 0, data);
}
#endif

#ifdef __lkl__NR_ppoll
/**
 * lkl_sys_poll - wrapper for lkl_sys_ppoll
 */
static inline long lkl_sys_poll(struct lkl_pollfd *fds, int n, int timeout)
{
	return lkl_sys_ppoll(fds, n, timeout >= 0 ?
			     (struct __lkl__kernel_timespec *)
			     &((struct lkl_timespec){ .tv_sec = timeout/1000,
				   .tv_nsec = timeout%1000*1000000 }) : 0,
			     0, _LKL_NSIG/8);
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


#ifdef __cplusplus
}
#endif

#endif
