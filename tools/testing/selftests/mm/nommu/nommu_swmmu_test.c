// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>



#include "kselftest.h"
#include "nommu_swmmu_user.h"

/*
 * FIXME: #include <linux/prctl.h>
 *
 * musl-libc is not reflecting latest header definition, thus a workaround.
 *
 */

#define PR_SET_SWMMU 82
#define PR_GET_SWMMU 83
#define PR_SWMMU_OFF 0
#define PR_SWMMU_ON  1

#define SWMMU_LIVE_CHILDREN		8
#define SWMMU_EXIT_CHURN_ITERS		32
#define SWMMU_EXIT_CHURN_MAPPINGS	16

/*
 * Force the runtime declarations to remain visible to the GCC plugin's
 * symbol lookup.
 */
static uint64_t (* const __attribute__((used))
keep_swmmu_load)(const void *, size_t) =
	nommu_swmmu_load_u64;

static void (* const __attribute__((used))
keep_swmmu_store)(void *, size_t, uint64_t) =
	nommu_swmmu_store_u64;

struct swmmu_test_object {
	uint64_t value;
};

__attribute__((swmmu, noinline))
static uint64_t
test_swmmu_increment(struct swmmu_test_object *object)
{
	object->value++;
	return object->value;
}

uint64_t nommu_swmmu_load_u64(const void *address,
			       size_t size)
{
	long ret;
	unsigned long long result;

	ret = syscall(SYS_nommu_swmmu_load,
		(uintptr_t)address,
		size,
		&result);

	if (ret < 0)
		abort();

	return (uint64_t)ret;
}

void nommu_swmmu_store_u64(void *address,
			   size_t size,
			   uint64_t value)
{
	long ret;

	ret = syscall(SYS_nommu_swmmu_store,
		(uintptr_t)address,
		size,
		value);
}

void *nommu_swmmu_alloc(size_t size)
{
	long ret;

	ret = syscall(SYS_nommu_swmmu_alloc, size);
	if (ret < 0)
		return NULL;

	return (void *)(uintptr_t)ret;
}

int nommu_swmmu_free(void *address)
{
	long ret;

	ret = syscall(SYS_nommu_swmmu_free,
		      (uintptr_t)address);

	if (ret < 0)
		return (int)ret;

	return 0;
}

void *nommu_swmmu_remap(void *address,
			size_t old_size,
			size_t new_size)
{
	long ret;

	ret = syscall(SYS_nommu_swmmu_remap,
		      (uintptr_t)address,
		      old_size,
		      new_size);

	if (ret < 0) {
		errno = -ret;
		return MAP_FAILED;
	}

	return (void *)(uintptr_t)ret;
}

static int
test_scalar_access(void)
{
	struct swmmu_test_object *object;
	uint64_t value;

	object = nommu_swmmu_alloc(sizeof(*object));
	if (!object) {
		ksft_test_result_skip("SWMMU allocation is unavailable\n");
		return KSFT_SKIP;
	}

	nommu_swmmu_store_u64(&object->value,
			      sizeof(object->value),
			      41);

	value = test_swmmu_increment(object);

	if (value != 42) {
		ksft_test_result_fail(
			"increment returned %llu, expected 42\n",
			(unsigned long long)value);
		nommu_swmmu_free(object);
		return KSFT_FAIL;
	}

	value = nommu_swmmu_load_u64(&object->value,
				     sizeof(object->value));

	if (value != 42) {
		ksft_test_result_fail(
			"load returned %llu, expected 42\n",
			(unsigned long long)value);
		nommu_swmmu_free(object);
		return KSFT_FAIL;
	}

	nommu_swmmu_free(object);

	ksft_test_result_pass(
		"compiler-generated SWMMU scalar access works\n");

	return KSFT_PASS;
}

struct child_result {
	uintptr_t address;
	uint64_t value;
};

static int
test_eager_copy_fork(void)
{
	struct swmmu_test_object *object;
	pid_t child_pid;
	pid_t waited_pid;
	int status;
	uint64_t parent_value;

	object = nommu_swmmu_alloc(sizeof(*object));
	if (!object) {
		ksft_test_result_skip("SWMMU allocation is unavailable\n");
		return KSFT_SKIP;
	}

	nommu_swmmu_store_u64(&object->value,
			      sizeof(object->value),
			      41);

	child_pid = fork();
	if (child_pid < 0) {
		ksft_test_result_fail("fork failed: %s\n",
				      strerror(errno));
		nommu_swmmu_free(object);
		return KSFT_FAIL;
	}

	if (child_pid == 0) {
		uint64_t value;

		if (prctl(PR_GET_SWMMU, 0, 0, 0, 0) != PR_SWMMU_ON)
			_exit(2);

		value = test_swmmu_increment(object);
		_exit(value == 42 ? 0 : 1);
	}

	status = -1;
	waited_pid = waitpid(child_pid, &status, 0);

	if (waited_pid < 0) {
		ksft_test_result_fail("waitpid failed: %s\n",
				strerror(errno));
		nommu_swmmu_free(object);
		return KSFT_FAIL;
	}

	if (waited_pid != child_pid) {
		ksft_test_result_fail(
			"waitpid returned %d, expected child pid %d\n",
			waited_pid, child_pid);
		nommu_swmmu_free(object);
		return KSFT_FAIL;
	}

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		ksft_test_result_fail(
			"child exited abnormally: status=%#x\n",
			status);
		nommu_swmmu_free(object);
		return KSFT_FAIL;
	}

	parent_value = nommu_swmmu_load_u64(&object->value,
					    sizeof(object->value));

	if (parent_value != 41) {
		ksft_test_result_fail(
			"parent value changed to %llu\n",
			(unsigned long long)parent_value);
		nommu_swmmu_free(object);
		return KSFT_FAIL;
	}

	nommu_swmmu_free(object);

	ksft_test_result_pass(
		"fork child exited and parent backing remained unchanged\n");

	return KSFT_PASS;
}

static int test_repeated_fork(void)
{
	struct swmmu_test_object *object;
	pid_t child_pid;
	pid_t waited_pid;
	int status;
	uint64_t parent_value;
	int i;

	for (i = 0; i < 128; i++) {
		/* allocate and initialize parent mapping */
		object = nommu_swmmu_alloc(sizeof(*object));
		if (!object) {
			ksft_test_result_skip("SWMMU allocation is unavailable\n");
			return KSFT_SKIP;
		}

		nommu_swmmu_store_u64(&object->value,
				sizeof(object->value),
				41);

		/* fork */
		child_pid = fork();
		if (child_pid < 0) {
			ksft_test_result_fail("fork failed: %s\n",
					strerror(errno));
			nommu_swmmu_free(object);
			return KSFT_FAIL;
		}

		/* child increments and exits */
		if (child_pid == 0) {
			uint64_t value;

			if (prctl(PR_GET_SWMMU, 0, 0, 0, 0) != PR_SWMMU_ON)
				_exit(2);

			value = test_swmmu_increment(object);
			_exit(value == 42 ? 0 : 1);
		}
		/* parent waits and verifies */
		status = -1;
		waited_pid = waitpid(child_pid, &status, 0);

		if (waited_pid < 0) {
			ksft_test_result_fail("waitpid failed: %s\n",
					strerror(errno));
			nommu_swmmu_free(object);
			return KSFT_FAIL;
		}

		if (waited_pid != child_pid) {
			ksft_test_result_fail(
				"waitpid returned %d, expected child pid %d\n",
				waited_pid, child_pid);
			nommu_swmmu_free(object);
			return KSFT_FAIL;
		}

		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			ksft_test_result_fail(
				"child exited abnormally: status=%#x\n",
				status);
			nommu_swmmu_free(object);
			return KSFT_FAIL;
		}

		/* parent verifies its value remains unchanged */
		parent_value = nommu_swmmu_load_u64(&object->value,
						sizeof(object->value));
		if (parent_value != 41) {
			ksft_test_result_fail(
				"parent value changed to %llu\n",
				(unsigned long long)parent_value);
			nommu_swmmu_free(object);
			return KSFT_FAIL;
		}

		/* free mapping */
		nommu_swmmu_free(object);
	}

	ksft_test_result_pass(
		"repeated fork works\n");
	return KSFT_PASS;
}

static int test_remap(void)
{
	void *base;
	void *new_base;
	uintptr_t second_page;
	uint64_t value;
	size_t ps;

	ps = sysconf(_SC_PAGESIZE);

	base = nommu_swmmu_alloc(ps);
	if (!base) {
		ksft_test_result_skip(
			"SWMMU allocation is unavailable\n");
		return KSFT_SKIP;
	}

	nommu_swmmu_store_u64(base, sizeof(uint64_t), 771);

	new_base = nommu_swmmu_remap(base,
				     ps,
				     ps * 2);
	if (new_base == MAP_FAILED) {
		ksft_test_result_fail(
			"SWMMU growth failed: %s\n",
			strerror(errno));
		nommu_swmmu_free(base);
		return KSFT_FAIL;
	}

	if (new_base != base) {
		ksft_test_result_fail(
			"SWMMU remap moved address from %p to %p\n",
			base, new_base);
		nommu_swmmu_free(new_base);
		return KSFT_FAIL;
	}

	value = nommu_swmmu_load_u64(base, sizeof(uint64_t));
	if (value != 771) {
		ksft_test_result_fail(
			"remap did not preserve value: %llu\n",
			(unsigned long long)value);
		nommu_swmmu_free(base);
		return KSFT_FAIL;
	}

	second_page = (uintptr_t)base + ps;

	value = nommu_swmmu_load_u64((void *)second_page,
				     sizeof(uint64_t));
	if (value != 0) {
		ksft_test_result_fail(
			"grown page was not zero-filled: %llu\n",
			(unsigned long long)value);
		nommu_swmmu_free(base);
		return KSFT_FAIL;
	}

	new_base = nommu_swmmu_remap(base,
				     ps * 2,
				     ps);
	if (new_base == MAP_FAILED) {
		ksft_test_result_fail(
			"SWMMU shrink failed: %s\n",
			strerror(errno));
		nommu_swmmu_free(base);
		return KSFT_FAIL;
	}

	if (new_base != base) {
		ksft_test_result_fail(
			"SWMMU shrink changed address\n");
		nommu_swmmu_free(new_base);
		return KSFT_FAIL;
	}

	nommu_swmmu_free(base);

	ksft_test_result_pass(
		"SWMMU remap growth and shrink work\n");

	return KSFT_PASS;
}

static int test_standard_mmap(void)
{
	void *base = MAP_FAILED;
	void *grown = MAP_FAILED;
	uint64_t value;
	size_t ps;
	int ret = KSFT_PASS;
	size_t mapped_len = 0;

	ps = sysconf(_SC_PAGESIZE);

	base = mmap(NULL, ps,
		    PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS,
		    -1, 0);
	if (base == MAP_FAILED) {
		ksft_test_result_fail("SWMMU mmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}
	mapped_len = ps;

	/*
	 * The annotated function must access this mapping through
	 * the compiler-generated SWMMU ABI.
	 */
	nommu_swmmu_store_u64(base, sizeof(uint64_t), 100);

	grown = mremap(base, ps, ps * 2, 0);
	if (grown == MAP_FAILED) {
		ksft_test_result_fail("SWMMU mremap failed: %s\n",
				      strerror(errno));
		ret = KSFT_FAIL;
		goto end;
	}
	mapped_len = ps * 2;

	value = nommu_swmmu_load_u64(grown, sizeof(uint64_t));
	if (value != 100) {
		ksft_test_result_fail("mmap value was not preserved\n");
		ret = KSFT_FAIL;
		goto end;
	}

	/* partial munmap → failure */
	if (munmap(grown, ps / 2) == 0) {
		ksft_test_result_fail("SWMMU munmap unexpectedly passed (partial munmap)\n");
		ret = KSFT_FAIL;
		goto end;
	}

	/* interior munmap → failure */
	if (munmap(grown + (ps / 2), ps / 4) == 0) {
		ksft_test_result_fail("SWMMU munmap unexpectedly passed (interior munmap)\n");
		ret = KSFT_FAIL;
		goto end;
	}

	/* mremap with incorrect old length → failure */
	grown = mremap(base, ps, ps * 2, 0);
	if (grown != MAP_FAILED) {
		ksft_test_result_fail("SWMMU mremap unexpectedly passed (incorrect old len)\n");
		ret = KSFT_FAIL;
		goto end;
	}

	/* mremap with MREMAP_MAYMOVE → failure initially */
	grown = mremap(base, ps * 2, ps, MREMAP_MAYMOVE);
	if (grown != MAP_FAILED) {
		ksft_test_result_fail("SWMMU mremap unexpectedly passed (MREMAP_MAYMOVE)\n");
		ret = KSFT_FAIL;
		goto end;
	}

	grown = mremap(base, ps * 2, ps, 0);
	if (grown == MAP_FAILED) {
		ksft_test_result_fail("SWMMU mremap (2nd) failed: %s\n",
				      strerror(errno));
		ret = KSFT_FAIL;
		goto end;
	}
	mapped_len = ps;

	if (munmap(grown, ps) != 0) {
		ksft_test_result_fail("SWMMU munmap failed: %s\n",
				      strerror(errno));
		ret = KSFT_FAIL;
		goto end;
	}

	ksft_test_result_pass("standard mmap/mremap/munmap use SWMMU\n");
end:
	/* ignore failure as it might be already failed */
	if (grown != MAP_FAILED && mapped_len)
		munmap(grown, mapped_len);
	return ret;
}

static int test_standard_mmap_fork(void)
{
	void *base;
	pid_t child_pid;
	pid_t waited_pid;
	int status;
	uint64_t value;
	size_t ps;

	ps = sysconf(_SC_PAGESIZE);

	base = mmap(NULL, ps,
		    PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS,
		    -1, 0);
	if (base == MAP_FAILED) {
		ksft_test_result_fail("SWMMU mmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}

	nommu_swmmu_store_u64(base, sizeof(uint64_t), 41);

	child_pid = fork();
	if (child_pid < 0) {
		ksft_test_result_fail("fork failed: %s\n",
				      strerror(errno));
		munmap(base, ps);
		return KSFT_FAIL;
	}

	if (child_pid == 0) {
		value = test_swmmu_increment(base);
		_exit(value == 42 ? 0 : 1);
	}

	waited_pid = waitpid(child_pid, &status, 0);
	if (waited_pid != child_pid ||
	    !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) {
		ksft_test_result_fail(
			"%s (child) failed: waited=%d child=%d status=%#x\n",
			__func__, waited_pid, child_pid, status);
		munmap(base, ps);
		return KSFT_FAIL;
	}

	value = nommu_swmmu_load_u64(base, sizeof(uint64_t));
	if (value != 41) {
		ksft_test_result_fail(
			"parent mapping changed after fork: %llu\n",
			(unsigned long long)value);
		munmap(base, ps);
		return KSFT_FAIL;
	}

	if (munmap(base, ps) != 0) {
		ksft_test_result_fail("standard mmap munmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}

	ksft_test_result_pass(
		"standard mmap mapping is eagerly copied across fork\n");

	return KSFT_PASS;
}

static int test_standard_mmap_fork_child_remap(void)
{
	void *base;
	void *new_base;
	pid_t child_pid;
	pid_t waited_pid;
	int status;
	uint64_t value;
	size_t ps;

	ps = sysconf(_SC_PAGESIZE);

	base = mmap(NULL, ps,
		    PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS,
		    -1, 0);
	if (base == MAP_FAILED) {
		ksft_test_result_fail("SWMMU mmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}

	nommu_swmmu_store_u64(base, sizeof(uint64_t), 41);

	child_pid = fork();
	if (child_pid < 0) {
		ksft_test_result_fail("fork failed: %s\n",
				      strerror(errno));
		munmap(base, ps);
		return KSFT_FAIL;
	}

	if (child_pid == 0) {
		new_base = mremap(base, ps, ps * 2, 0);
		if (new_base == MAP_FAILED) {
			ksft_print_msg("mremap failed: %s\n",
				strerror(errno));
			munmap(base, ps);
			_exit(1);
		}

		new_base = mremap(base, ps *2, ps, 0);
		if (new_base == MAP_FAILED) {
			ksft_print_msg("mremap failed: %s\n",
				strerror(errno));
			munmap(base, ps * 2);
			_exit(1);
		}

		value = test_swmmu_increment(new_base);
		_exit(value == 42 ? 0 : 1);
	}

	waited_pid = waitpid(child_pid, &status, 0);
	if (waited_pid != child_pid ||
	    !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) {
		ksft_test_result_fail(
			"%s (child) failed: waited=%d child=%d status=%#x\n",
			__func__, waited_pid, child_pid, status);
		munmap(base, ps);
		return KSFT_FAIL;
	}

	value = nommu_swmmu_load_u64(base, sizeof(uint64_t));
	if (value != 41) {
		ksft_test_result_fail(
			"parent mapping changed after fork: %llu\n",
			(unsigned long long)value);
		munmap(base, ps);
		return KSFT_FAIL;
	}

	if (munmap(base, ps) != 0) {
		ksft_test_result_fail("standard mmap munmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}

	ksft_test_result_pass("standard mmap mapping with child remap works\n");

	return KSFT_PASS;
}

static int test_standard_mmap_fork_unmap(void)
{
	void *base;
	pid_t child_pid;
	pid_t waited_pid;
	int status;
	uint64_t value;
	size_t ps;

	ps = sysconf(_SC_PAGESIZE);

	base = mmap(NULL, ps * 16,
		    PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS,
		    -1, 0);
	if (base == MAP_FAILED) {
		ksft_test_result_fail("SWMMU mmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}

	nommu_swmmu_store_u64(base, sizeof(uint64_t), 41);

	child_pid = fork();
	if (child_pid < 0) {
		ksft_test_result_fail("fork failed: %s\n",
				      strerror(errno));
		munmap(base, ps);
		return KSFT_FAIL;
	}

	if (child_pid == 0) {
		value = test_swmmu_increment(base);

		if (munmap(base, ps * 16) != 0) {
			ksft_print_msg("munmap in child failed: %s\n",
				strerror(errno));
			_exit(1);
		}

		_exit(value == 42 ? 0 : 1);
	}

	waited_pid = waitpid(child_pid, &status, 0);
	if (waited_pid != child_pid ||
	    !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) {
		ksft_test_result_fail(
			"%s (child) failed: waited=%d child=%d status=%#x\n",
			__func__, waited_pid, child_pid, status);
		munmap(base, ps);
		return KSFT_FAIL;
	}

	value = nommu_swmmu_load_u64(base, sizeof(uint64_t));
	if (value != 41) {
		ksft_test_result_fail(
			"parent mapping changed after fork: %llu\n",
			(unsigned long long)value);
		munmap(base, ps);
		return KSFT_FAIL;
	}

	if (munmap(base, ps * 16) != 0) {
		ksft_test_result_fail("standard mmap munmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}

	ksft_test_result_pass("standard mmap with child unmap works\n");

	return KSFT_PASS;
}

static int test_standard_mmap_exit_cleanup(void)
{
	void *base;
	pid_t child_pid;
	pid_t waited_pid;
	int status;
	uint64_t value;
	size_t ps;

	ps = sysconf(_SC_PAGESIZE);

	base = mmap(NULL, ps,
		    PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS,
		    -1, 0);
	if (base == MAP_FAILED) {
		ksft_test_result_fail("SWMMU mmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}

	nommu_swmmu_store_u64(base, sizeof(uint64_t), 41);

	child_pid = fork();
	if (child_pid < 0) {
		ksft_test_result_fail("fork failed: %s\n",
				      strerror(errno));
		munmap(base, ps);
		return KSFT_FAIL;
	}

	if (child_pid == 0) {
		value = test_swmmu_increment(base);

		base = mmap(NULL, ps,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS,
			-1, 0);
		if (base == MAP_FAILED) {
			ksft_print_msg(
				"SWMMU mmap (child) failed: %s\n",
				strerror(errno));
			_exit(1);
		}

		base = mremap(base, ps, ps * 2, 0);
		if (base == MAP_FAILED) {
			ksft_print_msg(
				"SWMMU mremap (child) failed: %s\n",
				strerror(errno));
			_exit(1);
		}

		base = mremap(base, ps * 2, ps, 0);
		if (base == MAP_FAILED) {
			ksft_print_msg(
				"SWMMU mremap (child, 2nd) failed: %s\n",
				strerror(errno));
			_exit(1);
		}

		/*
		 * expect unmap during exit_mmap() so,
		 * doesn't munmap() intentionally
		 */
		_exit(value == 42 ? 0 : 1);
	}

	waited_pid = waitpid(child_pid, &status, 0);
	if (waited_pid != child_pid ||
	    !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) {
		ksft_test_result_fail(
			"%s (child) failed: waited=%d child=%d status=%#x\n",
			__func__, waited_pid, child_pid, status);
		munmap(base, ps);
		return KSFT_FAIL;
	}

	value = nommu_swmmu_load_u64(base, sizeof(uint64_t));
	if (value != 41) {
		ksft_test_result_fail(
			"parent mapping changed after fork: %llu\n",
			(unsigned long long)value);
		munmap(base, ps);
		return KSFT_FAIL;
	}

	if (munmap(base, ps) != 0) {
		ksft_test_result_fail("standard mmap munmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}

	ksft_test_result_pass("standard mmap without child unmap works\n");

	return KSFT_PASS;
}

static int test_repeated_standard_mmap_fork(void)
{
	void *base;
	pid_t child_pid;
	pid_t waited_pid;
	int status;
	uint64_t value;
	size_t ps;

	for (int i = 0; i < 128; i++) {
		ps = sysconf(_SC_PAGESIZE);

		base = mmap(NULL, ps,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS,
			-1, 0);
		if (base == MAP_FAILED) {
			ksft_test_result_fail("SWMMU mmap failed: %s\n",
					strerror(errno));
			return KSFT_FAIL;
		}

		nommu_swmmu_store_u64(base, sizeof(uint64_t), 41);

		child_pid = fork();
		if (child_pid < 0) {
			ksft_test_result_fail("fork failed: %s\n",
					strerror(errno));
			munmap(base, ps);
			return KSFT_FAIL;
		}

		if (child_pid == 0) {
			value = test_swmmu_increment(base);
			_exit(value == 42 ? 0 : 1);
		}

		waited_pid = waitpid(child_pid, &status, 0);
		if (waited_pid != child_pid ||
			!WIFEXITED(status) ||
			WEXITSTATUS(status) != 0) {
			ksft_test_result_fail(
				"%s (child) failed: waited=%d child=%d status=%#x\n",
				__func__, waited_pid, child_pid, status);
			munmap(base, ps);
			return KSFT_FAIL;
		}

		value = nommu_swmmu_load_u64(base, sizeof(uint64_t));
		if (value != 41) {
			ksft_test_result_fail(
				"parent mapping changed after fork: %llu\n",
				(unsigned long long)value);
			munmap(base, ps);
			return KSFT_FAIL;
		}

		if (munmap(base, ps) != 0) {
			ksft_test_result_fail("standard mmap munmap failed: %s\n",
					strerror(errno));
			return KSFT_FAIL;
		}
	}

	ksft_test_result_pass("repeated mmap fork works\n");
	return KSFT_PASS;
}

static int test_multiple_standard_mmap_children(void)
{
	struct swmmu_test_object *object;
	pid_t children[SWMMU_LIVE_CHILDREN];
	pid_t child_pid;
	pid_t waited_pid;
	size_t ps;
	size_t child_count = 0;
	uint64_t value;
	int status;
	int failed = 0;
	int i;

	ps = sysconf(_SC_PAGESIZE);

	object = mmap(NULL, ps,
		      PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS,
		      -1, 0);
	if (object == MAP_FAILED) {
		ksft_test_result_fail("SWMMU mmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}

	nommu_swmmu_store_u64(&object->value,
			      sizeof(object->value),
			      41);

	for (i = 0; i < SWMMU_LIVE_CHILDREN; i++) {
		child_pid = fork();
		if (child_pid < 0) {
			ksft_test_result_fail(
				"fork %d failed: %s\n",
				i, strerror(errno));
			failed = 1;
			break;
		}

		if (child_pid == 0) {
			value = test_swmmu_increment(object);
			_exit(value == 42 ? 0 : 1);
		}

		children[child_count++] = child_pid;
	}

	for (i = 0; i < (int)child_count; i++) {
		waited_pid = waitpid(children[i], &status, 0);
		if (waited_pid != children[i] ||
		    !WIFEXITED(status) ||
		    WEXITSTATUS(status) != 0) {
			ksft_test_result_fail(
				"child %d failed: waited=%d status=%#x\n",
				(int)children[i],
				(int)waited_pid,
				status);
			failed = 1;
		}
	}

	value = nommu_swmmu_load_u64(&object->value,
				     sizeof(object->value));
	if (value != 41) {
		ksft_test_result_fail(
			"parent value changed to %llu\n",
			(unsigned long long)value);
		failed = 1;
	}

	if (munmap(object, ps) != 0) {
		ksft_test_result_fail(
			"parent munmap failed: %s\n",
			strerror(errno));
		failed = 1;
	}

	if (failed)
		return KSFT_FAIL;

	ksft_test_result_pass(
		"multiple live children retain independent SWMMU spaces\n");

	return KSFT_PASS;
}

static int test_standard_mmap_fork_child_isolation(void)
{
	struct swmmu_test_object *first;
	struct swmmu_test_object *second;
	void *second_hint;
	void *remapped;
	pid_t child_pid;
	pid_t waited_pid;
	size_t ps;
	uint64_t value;
	int status;

	ps = sysconf(_SC_PAGESIZE);

	first = mmap(NULL, ps, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS,
		     -1, 0);
	if (first == MAP_FAILED) {
		ksft_test_result_fail("first mmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}

	second_hint = (void *)((uintptr_t)first + 2 * ps);
	second = mmap(second_hint, ps * 2, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
		      -1, 0);
	if (second == MAP_FAILED || second != second_hint) {
		ksft_test_result_fail("second fixed mmap failed: %s\n",
				      strerror(errno));
		munmap(first, ps);
		return KSFT_FAIL;
	}

	nommu_swmmu_store_u64(&first->value,
			      sizeof(first->value), 41);
	nommu_swmmu_store_u64(&second->value,
			      sizeof(second->value), 81);

	child_pid = fork();
	if (child_pid < 0) {
		ksft_test_result_fail("fork failed: %s\n",
				      strerror(errno));
		munmap(second, ps * 2);
		munmap(first, ps);
		return KSFT_FAIL;
	}

	if (child_pid == 0) {
		uint64_t child_value;

		remapped = mremap(first, ps, ps * 2, 0);
		if (remapped == MAP_FAILED || remapped != first)
			_exit(1);

		remapped = mremap(first, ps * 2, ps, 0);
		if (remapped == MAP_FAILED || remapped != first)
			_exit(2);

		child_value = test_swmmu_increment(first);
		if (child_value != 42)
			_exit(3);

		if (munmap(second, ps * 2) != 0)
			_exit(4);

		/*
		 * Leave first mapped. exit_mmap() will clean it up in
		 * the child.
		 */
		_exit(0);
	}

	waited_pid = waitpid(child_pid, &status, 0);
	if (waited_pid != child_pid ||
	    !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) {
		ksft_test_result_fail(
			"child failed: waited=%d status=%#x\n",
			(int)waited_pid,
			status);
		munmap(second, ps * 2);
		munmap(first, ps);
		return KSFT_FAIL;
	}

	value = nommu_swmmu_load_u64(&first->value,
				     sizeof(first->value));
	if (value != 41) {
		ksft_test_result_fail(
			"parent first mapping changed to %llu\n",
			(unsigned long long)value);
		munmap(second, ps * 2);
		munmap(first, ps);
		return KSFT_FAIL;
	}

	value = nommu_swmmu_load_u64(&second->value,
				     sizeof(second->value));
	if (value != 81) {
		ksft_test_result_fail(
			"parent second mapping changed to %llu\n",
			(unsigned long long)value);
		munmap(second, ps * 2);
		munmap(first, ps);
		return KSFT_FAIL;
	}

	if (munmap(second, ps * 2) != 0) {
		ksft_test_result_fail(
			"parent second munmap failed: %s\n",
			strerror(errno));
		munmap(first, ps);
		return KSFT_FAIL;
	}

	if (munmap(first, ps) != 0) {
		ksft_test_result_fail(
			"parent first munmap failed: %s\n",
			strerror(errno));
		return KSFT_FAIL;
	}

	ksft_test_result_pass(
		"child remap/unmap does not affect parent mappings\n");

	return KSFT_PASS;
}

static int test_standard_mmap_exit_cleanup_churn(void)
{
	pid_t child_pid;
	pid_t waited_pid;
	size_t ps;
	int status;
	int i;
	int j;

	ps = sysconf(_SC_PAGESIZE);

	for (i = 0; i < SWMMU_EXIT_CHURN_ITERS; i++) {
		child_pid = fork();
		if (child_pid < 0) {
			ksft_test_result_fail(
				"fork iteration %d failed: %s\n",
				i, strerror(errno));
			return KSFT_FAIL;
		}

		if (child_pid == 0) {
			void *mappings[SWMMU_EXIT_CHURN_MAPPINGS];

			for (j = 0; j < SWMMU_EXIT_CHURN_MAPPINGS; j++) {
				void *remapped;
				uint64_t value;

				mappings[j] = mmap(NULL, ps,
						    PROT_READ | PROT_WRITE,
						    MAP_PRIVATE | MAP_ANONYMOUS,
						    -1, 0);
				if (mappings[j] == MAP_FAILED)
					_exit(1);

				value = (uint64_t)(j + 1);
				nommu_swmmu_store_u64(mappings[j],
						      sizeof(value),
						      value);

				/*
				 * Grow before creating the next mapping so
				 * the extension is not adjacent to an existing
				 * mapping.
				 */
				if ((j & 1) == 0) {
					remapped = mremap(mappings[j],
							  ps,
							  ps * 2,
							  0);
					if (remapped == MAP_FAILED ||
					    remapped != mappings[j])
						_exit(1);

					remapped = mremap(mappings[j],
							  ps * 2,
							  ps,
							  0);
					if (remapped == MAP_FAILED ||
					    remapped != mappings[j])
						_exit(1);
				}
			}

			/*
			 * Do not call munmap(). exit_mmap() must reclaim
			 * every mapping.
			 */
			_exit(0);
		}

		waited_pid = waitpid(child_pid, &status, 0);
		if (waited_pid != child_pid ||
		    !WIFEXITED(status) ||
		    WEXITSTATUS(status) != 0) {
			ksft_test_result_fail(
				"exit-cleanup child %d failed: "
				"waited=%d status=%#x\n",
				i, (int)waited_pid, status);
			return KSFT_FAIL;
		}
	}

	ksft_test_result_pass(
		"repeated SWMMU exit cleanup succeeds\n");

	return KSFT_PASS;
}

static int test_standard_mmap_permission(void)
{
	size_t ps;
	void *ret;
	int result = KSFT_FAIL;
	void *base = MAP_FAILED;
	size_t mapped_len = 0;

	ps = sysconf(_SC_PAGESIZE);

	/* mmap(PROT_READ) */
	base = mmap(NULL, ps, PROT_READ,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED) {
		ksft_test_result_fail("mmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}
	/* this should not cause BUG() in kernel */
	nommu_swmmu_load_u64(base, sizeof(uint64_t));
	if (munmap(base, ps) != 0) {
		ksft_test_result_fail("munmap 0 failed: %s\n",
			strerror(errno));
		return KSFT_FAIL;
	}

	/* mmap(PROT_READ | PROT_WRITE) */
	base = mmap(NULL, ps, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED) {
		ksft_test_result_fail("mmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}
	/* this should not cause BUG() in kernel */
	nommu_swmmu_load_u64(base, sizeof(uint64_t));
	nommu_swmmu_store_u64(base, sizeof(uint64_t), 1919);
	if (munmap(base, ps) != 0) {
		ksft_test_result_fail("munmap 1 failed: %s\n",
			strerror(errno));
		return KSFT_FAIL;
	}

	/* mmap(PROT_NONE) */
	base = mmap(NULL, ps, PROT_NONE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED) {
		ksft_test_result_fail("mmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}
	/* any ops should cause BUG() in kernel, so do nothing */
	if (munmap(base, ps) != 0) {
		ksft_test_result_fail("munmap 2 failed: %s\n",
			strerror(errno));
		return KSFT_FAIL;
	}

	/* MAP_FIXED */
	base = mmap((void *)0x2000000000ULL, ps, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (base == MAP_FAILED) {
		ksft_test_result_fail("mmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}

	/* MAP_FIXED_NOREPLACE */
	ret = mmap((void *)0x2000000000ULL, ps, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (ret != MAP_FAILED || errno != EEXIST) {
		ksft_test_result_fail(
			"MAP_FIXED_NOREPLACE returned unexpected result: "
			"ret=%p errno=%d\n",
			ret, errno);

		result = KSFT_FAIL;
		mapped_len = ps;
		goto out;
	}

	if (munmap(base, ps) != 0) {
		ksft_test_result_fail("munmap 3 failed: %s\n",
			strerror(errno));
		return KSFT_FAIL;
	}

	result = KSFT_PASS;
out:
	if (base != MAP_FAILED)
		munmap(base, mapped_len);

	ksft_test_result_pass(
		"standard mmap permission and fixed-address checks work\n");
	return result;
}

static int test_standard_mmap_nonzero_hint(void)
{
	size_t ps;
	void *base = MAP_FAILED;

	ps = sysconf(_SC_PAGESIZE);

	/* returned address is the hint when it is free */
	base = mmap((void *)0x1000000000UL, ps, PROT_READ,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED) {
		ksft_test_result_fail("mmap failed: %s\n",
				      strerror(errno));
		return KSFT_FAIL;
	}

	if (base != (void *)0x1000000000UL) {
		ksft_test_result_fail("mmap returns non-hinted address %p: %s\n",
				base, strerror(errno));
		return KSFT_FAIL;
	}

	if (base != MAP_FAILED)
		munmap(base, ps);

	ksft_test_result_pass(
		"standard mmap with nonzero hint test works\n");
	return KSFT_PASS;
}

static int test_standard_mmap_occupied_hint(void)
{
	size_t ps;
	void *fixed, *hint;

	ps = sysconf(_SC_PAGESIZE);

	/* returned address differs from the hint */
	fixed = mmap((void *)0x2000000000UL, ps, PROT_READ,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (fixed == MAP_FAILED || fixed != (void *)0x2000000000UL) {
		ksft_test_result_fail("mmap fixed failed (%p): %s\n",
				fixed, strerror(errno));
		return KSFT_FAIL;
	}

	hint = mmap((void *)0x2000000000UL, ps, PROT_READ,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (hint == MAP_FAILED || hint == (void *)0x2000000000UL) {
		ksft_test_result_fail("mmap with hint failed (%p): %s\n",
				hint, strerror(errno));
		munmap(fixed, ps);
		return KSFT_FAIL;
	}

	if (fixed != MAP_FAILED)
		munmap(fixed, ps);

	if (hint != MAP_FAILED)
		munmap(hint, ps);

	ksft_test_result_pass(
		"standard mmap with occupied hint test works\n");
	return KSFT_PASS;
}

static int test_standard_mmap_fixed_noreplace(void)
{
	size_t ps;
	void *addr1, *addr2;

	ps = sysconf(_SC_PAGESIZE);

	/* returned address differs from the hint */
	addr1 = mmap((void *)0x2000000000UL, ps, PROT_READ,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (addr1 == MAP_FAILED || addr1 != (void *)0x2000000000UL) {
		ksft_test_result_fail("mmap fixed failed (%p): %s\n",
				addr1, strerror(errno));
		return KSFT_FAIL;
	}

	addr2 = mmap((void *)0x2000000000UL, ps, PROT_READ,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (addr2 != MAP_FAILED || errno != EEXIST) {
		ksft_test_result_fail("mmap with FIXED_NOREPLACE failed (%p)): %s\n",
				addr2, strerror(errno));
		munmap(addr1, ps);
		return KSFT_FAIL;
	}

	if (addr2 != MAP_FAILED)
		munmap(addr2, ps);
	if (addr1 != MAP_FAILED)
		munmap(addr1, ps);

	ksft_test_result_pass(
		"standard mmap with MAP_FIXED_NOREPLACE test works\n");
	return KSFT_PASS;
}

int main(void)
{
	int result = KSFT_PASS;

	if (prctl(PR_SET_SWMMU, PR_SWMMU_ON, 0, 0, 0) < 0) {
		ksft_test_result_skip("SWMMU prctl unavailable: %s\n",
				strerror(errno));
		return KSFT_SKIP;
	}

	ksft_print_header();
	ksft_set_plan(17);

	if (test_scalar_access() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_eager_copy_fork() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_repeated_fork() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_remap() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_standard_mmap() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_standard_mmap_fork() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_standard_mmap_fork_child_remap() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_standard_mmap_fork_unmap() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_repeated_standard_mmap_fork() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_multiple_standard_mmap_children() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_standard_mmap_fork_child_isolation() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_standard_mmap_permission() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_standard_mmap_nonzero_hint() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_standard_mmap_occupied_hint() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_standard_mmap_fixed_noreplace() == KSFT_FAIL)
		result = KSFT_FAIL;


	/* shall be the last test */
	if (test_standard_mmap_exit_cleanup_churn() == KSFT_FAIL)
		result = KSFT_FAIL;
	if (test_standard_mmap_exit_cleanup() == KSFT_FAIL)
		result = KSFT_FAIL;


	if (result == KSFT_PASS)
		ksft_finished();

	ksft_exit_fail();
}
