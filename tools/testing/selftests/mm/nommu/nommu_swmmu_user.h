/* userspace ABI: can be in libc */
#ifndef NOMMU_SWMMU_USER_H
#define NOMMU_SWMMU_USER_H

#include <stddef.h>
#include <stdint.h>

#define SYS_nommu_swmmu_alloc 472
#define SYS_nommu_swmmu_free 473
#define SYS_nommu_swmmu_load 474
#define SYS_nommu_swmmu_store 475

void *nommu_swmmu_alloc(size_t size);
int nommu_swmmu_free(void *address);

uint64_t nommu_swmmu_load_u64(const void *address, size_t size);

void nommu_swmmu_store_u64(void *address,
			   size_t size,
			   uint64_t value);

#endif
