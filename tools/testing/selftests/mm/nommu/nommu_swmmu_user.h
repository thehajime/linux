/* userspace ABI: can be in libc */
#ifndef NOMMU_SWMMU_USER_H
#define NOMMU_SWMMU_USER_H

#include <stddef.h>
#include <stdint.h>
#include <linux/nommu_swmmu.h>
#include <sys/syscall.h>
#include <asm/unistd.h>

void *nommu_swmmu_alloc(size_t size);
int nommu_swmmu_free(void *address);

uint64_t nommu_swmmu_load_u64(const void *, size_t);
long nommu_swmmu_store_u64(void *, size_t, uint64_t);
int nommu_swmmu_load_u64_checked(const void *, size_t, uint64_t *);
int nommu_swmmu_store_u64_checked(void *, size_t, uint64_t);

void *nommu_swmmu_memcpy(void *, const void *, size_t);
void *nommu_swmmu_memmove(void *, const void *, size_t);
void *nommu_swmmu_memset(void *, int, size_t);

uint64_t nommu_swmmu_load_dynamic(const void *, size_t);
long nommu_swmmu_store_dynamic(void *, size_t, uint64_t);
int nommu_swmmu_load_dynamic_checked(const void *, size_t, uint64_t *);
int nommu_swmmu_store_dynamic_checked(void *, size_t, uint64_t);
void *nommu_swmmu_memcpy_dynamic(void *, const void *, size_t);
void *nommu_swmmu_memmove_dynamic(void *, const void *, size_t);
void *nommu_swmmu_memset_dynamic(void *, int, size_t);
int nommu_swmmu_cmpxchg_u32(void *, uint32_t, uint32_t, uint32_t *);

#ifndef SYS_nommu_swmmu_alloc
#define SYS_nommu_swmmu_alloc __NR_nommu_swmmu_alloc
#endif

#ifndef SYS_nommu_swmmu_free
#define SYS_nommu_swmmu_free __NR_nommu_swmmu_free
#endif

#ifndef SYS_nommu_swmmu_load
#define SYS_nommu_swmmu_load __NR_nommu_swmmu_load
#endif

#ifndef SYS_nommu_swmmu_store
#define SYS_nommu_swmmu_store __NR_nommu_swmmu_store
#endif

#ifndef SYS_nommu_swmmu_cmpxchg
#define SYS_nommu_swmmu_cmpxchg __NR_nommu_swmmu_cmpxchg
#endif


#endif
