#include <stdio.h>
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
		(uintptr_t)address, size, &result, NOMMU_SWMMU_ACCESS_SIGNAL);
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
		(uintptr_t)address, size, value, NOMMU_SWMMU_ACCESS_SIGNAL);
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
		      (uintptr_t)address, size, result,
		      NOMMU_SWMMU_ACCESS_CHECKED);

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
		      (uintptr_t)address, size, value,
		      NOMMU_SWMMU_ACCESS_CHECKED);

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

void *nommu_swmmu_memcpy(void *dst,
			 const void *src,
			 size_t size)
{
	unsigned char *destination = dst;

	while (size--) {
		uint64_t val;

		val = nommu_swmmu_load_u64(src, 1);
		nommu_swmmu_store_u64(dst, 1, val);

		src++;
		dst++;
	}

	return destination;
}

void *nommu_swmmu_memmove(void *dst, const void *src, size_t size)
{
	uintptr_t dst_addr = (uintptr_t)dst;
	uintptr_t src_addr = (uintptr_t)src;
	size_t i;

	if (!size || dst_addr == src_addr)
		return dst;

	if (dst_addr < src_addr ||
	    dst_addr - src_addr >= size) {
		for (i = 0; i < size; i++) {
			uint64_t value;

			value = nommu_swmmu_load_u64(
				(const void *)(src_addr + i), 1);
			nommu_swmmu_store_u64(
				(void *)(dst_addr + i), 1, value);
		}
	} else {
		for (i = size; i > 0; i--) {
			size_t offset = i - 1;
			uint64_t value;

			value = nommu_swmmu_load_u64(
				(const void *)(src_addr + offset), 1);
			nommu_swmmu_store_u64(
				(void *)(dst_addr + offset), 1, value);
		}
	}

	return dst;
}

void *nommu_swmmu_memset(void *dst, int b, size_t size)
{
	char *p = dst;

	while (size--) {
		nommu_swmmu_store_u64(p, 1, (uint64_t)b);
		p++;
	}
	return dst;
}
