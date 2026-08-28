#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <unistd.h>

#include "nommu_swmmu_user.h"

uint64_t nommu_swmmu_load_u64(const void *address,
			       size_t size)
{
	uint64_t result;
	long ret;

	ret = syscall(SYS_nommu_swmmu_load,
		(uintptr_t)address,
		size,
		&result);
	if (ret < 0)
		abort();

	return result;
}

long nommu_swmmu_store_u64(void *address,
			   size_t size,
			   uint64_t value)
{
	long ret;

	ret = syscall(SYS_nommu_swmmu_store,
		(uintptr_t)address,
		size,
		value);
	if (ret < 0)
		abort();

	return ret;
}

int nommu_swmmu_load_u64_checked(const void *address,
				 size_t size,
				 uint64_t *result)
{
	long ret;

	if (!result)
		return -EINVAL;

	ret = syscall(SYS_nommu_swmmu_load,
		      (uintptr_t)address,
		      size,
		      result);

	if (ret < 0)
		return -errno;

	return 0;
}

int nommu_swmmu_store_u64_checked(void *address,
				  size_t size,
				  uint64_t value)
{
	long ret;

	ret = syscall(SYS_nommu_swmmu_store,
		      (uintptr_t)address,
		      size,
		      value);

	if (ret < 0)
		return -errno;

	return 0;
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

	if (ret == -1)
		return -errno;

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

	if (ret == -1)
		return MAP_FAILED;

	return (void *)(uintptr_t)ret;
}
