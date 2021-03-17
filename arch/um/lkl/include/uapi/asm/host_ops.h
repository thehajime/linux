/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_UAPI_HOST_OPS_H
#define __UM_LIBMODE_UAPI_HOST_OPS_H

struct lkl_mutex;
struct lkl_sem;
typedef unsigned long lkl_thread_t;
struct lkl_jmp_buf {
	unsigned long buf[128];
};

/**
 * struct lkl_host_operations - host operations used by the Linux kernel
 *
 * These operations must be provided by a host library or by the application
 * itself.
 *
 */
struct lkl_host_operations {
};

/**
 * lkl_bug - call lkl_panic with a message
 *
 * @fmt: message format with parameters
 *
 */
void lkl_bug(const char *fmt, ...);

/**
 * lkl_mem_alloc - allocate memory
 *
 * @mem: the size of memory requested
 *
 */
void *lkl_mem_alloc(unsigned long mem);

/**
 * lkl_mem_free - free memory
 *
 * @mem: the address of memory to be freed
 *
 */
void lkl_mem_free(void *mem);

/**
 * lkl_sem_alloc - allocate a host semaphore an initialize it to count
 *
 * @count: initial counter value
 *
 * Return: the pointer of allocated semaphore
 *
 */
struct lkl_sem *lkl_sem_alloc(int count);

/**
 * lkl_sem_free - free a host semaphore
 *
 * @sem: the address of semaphore to be freed
 *
 */
void lkl_sem_free(struct lkl_sem *sem);

/**
 * lkl_sem_up - perform an up operation on the semaphore
 *
 * @sem: semaphore pointer address
 *
 */
void lkl_sem_up(struct lkl_sem *sem);

/**
 * lkl_sem_down - perform a down operation on the semaphore
 *
 * @sem: semaphore pointer address
 *
 */
void lkl_sem_down(struct lkl_sem *sem);

/**
 * lkl_mutex_alloc - allocate and initialize a host mutex
 *
 * @recursive: boolean flag if the mutex is recursive or not
 *
 * Return: the pointer of allocated mutex
 *
 */
struct lkl_mutex *lkl_mutex_alloc(int recursive);

/**
 * lkl_mutex_lock - acquire the mutex
 *
 * @_mutex: the mutex pointer to be locked
 *
 */
void lkl_mutex_lock(struct lkl_mutex *mutex);

/**
 * lkl_mutex_unlock - release the mutex
 *
 * @_mutex: the mutex pointer to be released
 *
 */
void lkl_mutex_unlock(struct lkl_mutex *_mutex);

/**
 * lkl_mutex_free - free a host mutex
 *
 * @mutex: the mutex pointer to be freed
 *
 */
void lkl_mutex_free(struct lkl_mutex *_mutex);

/**
 * lkl_thread_create - create a new thread and run f(arg) in its context
 *
 * @fn: a start routine when creating a thread
 * @arg: an argument for the function @fn
 *
 * Return: a thread handle or 0 if the thread could not be created
 *
 */
lkl_thread_t lkl_thread_create(void* (*fn)(void *), void *arg);

/**
 * lkl_thread_detach - on POSIX systems, free up resources held by
 * pthreads.
 *
 */
void lkl_thread_detach(void);

/**
 * lkl_thread_exit - terminates the current thread
 *
 */
void lkl_thread_exit(void);

/**
 * lkl_thread_join - wait for the given thread to terminate.
 *
 * @tid: thread id to wait
 *
 * Return: 0 for success, -1 otherwise
 *
 */
int lkl_thread_join(lkl_thread_t tid);

/**
 * lkl_thread_self - returns the identifier of the current thread
 *
 * Return: the identifier of the current thread
 *
 */
lkl_thread_t lkl_thread_self(void);

/**
 * lkl_thread_equal - compare if thread a and b are identical or not
 *
 * @a: a thread to be compared
 * @b: another thread to be compared with @a
 *
 * Return: 1 if @a and @b are same; otherwise 0
 *
 */
int lkl_thread_equal(lkl_thread_t a, lkl_thread_t b);

/**
 * lkl_gettid - obtain the thread identifier of the current thread
 *
 * Return: the host thread id of the caller, which need not be the same
 * as the handle returned by thread_create
 *
 */
long lkl_gettid(void);

/**
 * lkl_jmp_buf_set - set the jump buffer with a setup function
 *
 * @jmpb: jump buffer to be saved
 * @f: a function to be called
 *
 * This runs the given function and setups a jump back point by saving
 * the context in the jump buffer; jmp_buf_longjmp can be called from the give
 * function or any callee in that function to return back to the jump back
 * point.
 *
 * NOTE: we can't return from lkl_jmp_buf_set before calling lkl_jmp_buf_longjmp
 * or otherwise the saved context (stack) is not going to be valid, so we must
 * pass the function that will eventually call longjmp here.
 *
 */
void lkl_jmp_buf_set(struct lkl_jmp_buf *jmpb, void (*f)(void));

/**
 * lkl_jmp_buf_longjmp - perform a jump back to the saved jump buffer
 *
 * @jmpb: jump buffer to be restored
 * @val: returned value of longjmp
 *
 */
void lkl_jmp_buf_longjmp(struct lkl_jmp_buf *jmpb, int val);

#endif
