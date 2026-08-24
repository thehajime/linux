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

#include "kselftest.h"
#include "nommu_swmmu_user.h"

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

int main(void)
{
	int result = KSFT_PASS;

	ksft_print_header();
	ksft_set_plan(3);

	if (test_scalar_access() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_eager_copy_fork() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (test_repeated_fork() == KSFT_FAIL)
		result = KSFT_FAIL;

	if (result == KSFT_PASS)
		ksft_finished();

	ksft_exit_fail();
}
