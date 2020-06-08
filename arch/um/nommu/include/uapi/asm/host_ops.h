/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __UM_NOMMU_UAPI_HOST_OPS_H
#define __UM_NOMMU_UAPI_HOST_OPS_H

/* Defined in {posix,nt}-host.c */
struct lkl_mutex;
struct lkl_sem;
typedef unsigned long lkl_thread_t;

/**
 * lkl_host_operations - host operations used by the Linux kernel
 *
 * These operations must be provided by a host library or by the application
 * itself.
 * @print - optional operation that receives console messages
 * @panic - called during a kernel panic
 *
 * @sem_alloc - allocate a host semaphore an initialize it to count
 * @sem_free - free a host semaphore
 * @sem_up - perform an up operation on the semaphore
 * @sem_down - perform a down operation on the semaphore
 *
 * @mutex_alloc - allocate and initialize a host mutex; the recursive parameter
 * determines if the mutex is recursive or not
 * @mutex_free - free a host mutex
 * @mutex_lock - acquire the mutex
 * @mutex_unlock - release the mutex
 *
 * @thread_create - create a new thread and run f(arg) in its context; returns a
 * thread handle or 0 if the thread could not be created
 * @thread_detach - on POSIX systems, free up resources held by
 * pthreads. Noop on Win32.
 * @thread_exit - terminates the current thread
 * @thread_join - wait for the given thread to terminate. Returns 0
 * for success, -1 otherwise
 *
 * @gettid - returns the host thread id of the caller, which need not
 * be the same as the handle returned by thread_create
 *
 * @mem_alloc - allocate memory
 * @mem_free - free memory
 *
 */
struct lkl_host_operations {
	void (*print)(const char *str, int len);
	void (*panic)(void);

	struct lkl_sem *(*sem_alloc)(int count);
	void (*sem_free)(struct lkl_sem *sem);
	void (*sem_up)(struct lkl_sem *sem);
	void (*sem_down)(struct lkl_sem *sem);

	struct lkl_mutex *(*mutex_alloc)(int recursive);
	void (*mutex_free)(struct lkl_mutex *mutex);
	void (*mutex_lock)(struct lkl_mutex *mutex);
	void (*mutex_unlock)(struct lkl_mutex *mutex);

	lkl_thread_t (*thread_create)(void *(*f)(void *), void *arg);
	void (*thread_detach)(void);
	void (*thread_exit)(void);
	int (*thread_join)(lkl_thread_t tid);
	lkl_thread_t (*thread_self)(void);
	int (*thread_equal)(lkl_thread_t a, lkl_thread_t b);
	long (*gettid)(void);

	void *(*mem_alloc)(unsigned long mem);
	void (*mem_free)(void *mem);
};

int lkl_start_kernel(struct lkl_host_operations *ops,
		     const char *fmt, ...);

#endif
