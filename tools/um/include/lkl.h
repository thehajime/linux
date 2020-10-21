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

#ifdef __lkl__NR_llseek
/**
 * lkl_sys_lseek - wrapper for lkl_sys_llseek
 */
static inline long long lkl_sys_lseek(unsigned int fd, __lkl__kernel_loff_t off,
				      unsigned int whence)
{
	long long res;
	long ret = lkl_sys_llseek(fd, off >> 32, off & 0xffffffff, &res, whence);

	return ret < 0 ? ret : res;
}
#endif


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

#ifdef __lkl__NR_unlinkat
/**
 * lkl_sys_unlink - wrapper for lkl_sys_unlinkat
 */
static inline long lkl_sys_unlink(const char *path)
{
	return lkl_sys_unlinkat(LKL_AT_FDCWD, path, 0);
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
 * struct lkl_dev_blk_ops - block device host operations, defined in lkl_host.h.
 */
struct lkl_dev_blk_ops;

/**
 * lkl_disk - host disk handle
 *
 * @dev - a pointer to private information for this disk backend
 * @fd - a POSIX file descriptor that can be used by preadv/pwritev
 * @handle - an NT file handle that can be used by ReadFile/WriteFile
 */
struct lkl_disk {
	void *dev;
	union {
		int fd;
		void *handle;
	};
	struct lkl_dev_blk_ops *ops;
};

/**
 * lkl_disk_add - add a new disk
 *
 * @disk - the host disk handle
 * @returns a disk id (0 is valid) or a strictly negative value in case of error
 */
int lkl_disk_add(struct lkl_disk *disk);

/**
 * lkl_disk_remove - remove a disk
 *
 * This function makes a cleanup of the @disk's private information
 * that was initialized by lkl_disk_add before.
 *
 * @disk - the host disk handle
 */
int lkl_disk_remove(struct lkl_disk disk);

/**
 * lkl_encode_dev_from_sysfs_blkdev - extract device id from sysfs
 *
 * This function returns the device id for the given sysfs dev node.
 * The content of the node has to be in the form 'MAJOR:MINOR'.
 * Also, this function expects an absolute path which means that sysfs
 * already has to be mounted at the given path
 *
 * @sysfs_path - absolute path to the sysfs dev node
 * @pdevid - pointer to memory where dev id will be returned
 * @returns - 0 on success, a negative value on error
 */
int lkl_encode_dev_from_sysfs(const char *sysfs_path, uint32_t *pdevid);

/**
 * lkl_mount_dev - mount a disk
 *
 * This functions creates a device file for the given disk, creates a mount
 * point and mounts the device over the mount point.
 *
 * @disk_id - the disk id identifying the disk to be mounted
 * @part - disk partition or zero for full disk
 * @fs_type - filesystem type
 * @flags - mount flags
 * @opts - additional filesystem specific mount options
 * @mnt_str - a string that will be filled by this function with the path where
 * the filesystem has been mounted
 * @mnt_str_len - size of mnt_str
 * @returns - 0 on success, a negative value on error
 */
long lkl_mount_dev(unsigned int disk_id, unsigned int part, const char *fs_type,
		   int flags, const char *opts,
		   char *mnt_str, unsigned int mnt_str_len);

/**
 * lkl_umount_dev - umount a disk
 *
 * This functions umounts the given disks and removes the device file and the
 * mount point.
 *
 * @disk_id - the disk id identifying the disk to be mounted
 * @part - disk partition or zero for full disk
 * @flags - umount flags
 * @timeout_ms - timeout to wait for the kernel to flush closed files so that
 * umount can succeed
 * @returns - 0 on success, a negative value on error
 */
long lkl_umount_dev(unsigned int disk_id, unsigned int part, int flags,
		    long timeout_ms);

/**
 * lkl_umount_timeout - umount filesystem with timeout
 *
 * @path - the path to unmount
 * @flags - umount flags
 * @timeout_ms - timeout to wait for the kernel to flush closed files so that
 * umount can succeed
 * @returns - 0 on success, a negative value on error
 */
long lkl_umount_timeout(char *path, int flags, long timeout_ms);

/**
 * lkl_opendir - open a directory
 *
 * @path - directory path
 * @err - pointer to store the error in case of failure
 * @returns - a handle to be used when calling lkl_readdir
 */
struct lkl_dir *lkl_opendir(const char *path, int *err);

/**
 * lkl_fdopendir - open a directory
 *
 * @fd - file descriptor
 * @err - pointer to store the error in case of failure
 * @returns - a handle to be used when calling lkl_readdir
 */
struct lkl_dir *lkl_fdopendir(int fd, int *err);

/**
 * lkl_rewinddir - reset directory stream
 *
 * @dir - the directory handler as returned by lkl_opendir
 */
void lkl_rewinddir(struct lkl_dir *dir);

/**
 * lkl_closedir - close the directory
 *
 * @dir - the directory handler as returned by lkl_opendir
 */
int lkl_closedir(struct lkl_dir *dir);

/**
 * lkl_readdir - get the next available entry of the directory
 *
 * @dir - the directory handler as returned by lkl_opendir
 * @returns - a lkl_dirent64 entry or NULL if the end of the directory stream is
 * reached or if an error occurred; check lkl_errdir() to distinguish between
 * errors or end of the directory stream
 */
struct lkl_linux_dirent64 *lkl_readdir(struct lkl_dir *dir);

/**
 * lkl_errdir - checks if an error occurred during the last lkl_readdir call
 *
 * @dir - the directory handler as returned by lkl_opendir
 * @returns - 0 if no error occurred, or a negative value otherwise
 */
int lkl_errdir(struct lkl_dir *dir);

/**
 * lkl_dirfd - gets the file descriptor associated with the directory handle
 *
 * @dir - the directory handle as returned by lkl_opendir
 * @returns - a positive value,which is the LKL file descriptor associated with
 * the directory handle, or a negative value otherwise
 */
int lkl_dirfd(struct lkl_dir *dir);

/**
 * lkl_mount_fs - mount a file system type like proc, sys
 * @fstype - file system type. e.g. proc, sys
 * @returns - 0 on success. 1 if it's already mounted. negative on failure.
 */
int lkl_mount_fs(char *fstype);

#ifdef __cplusplus
}
#endif

#endif
