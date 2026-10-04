#include <stddef.h>
#include <stdint.h>

extern uint64_t nommu_swmmu_load_dynamic(
	const void *, size_t);

extern long nommu_swmmu_store_dynamic(
	void *, size_t, uint64_t);

extern void *nommu_swmmu_memcpy_dynamic(
	void *, const void *, size_t);

extern void *nommu_swmmu_memmove_dynamic(
	void *, const void *, size_t);

extern void *nommu_swmmu_memset_dynamic(
	void *, int, size_t);

extern uint64_t nommu_swmmu_bitfield_load_dynamic(
	const void *, size_t, unsigned int, unsigned int);

extern long nommu_swmmu_bitfield_store_dynamic(
	void *, size_t, unsigned int, unsigned int, uint64_t);

extern uint64_t nommu_swmmu_load_u64(const void *address, size_t size);
extern long nommu_swmmu_store_u64(void *address,
				  size_t size,
				  uint64_t value);

extern uint64_t nommu_swmmu_load_dynamic(const void *address,
					 size_t size);

extern long nommu_swmmu_store_dynamic(void *address,
				      size_t size,
				      uint64_t value);

extern void *nommu_swmmu_memcpy_dynamic(void *destination,
					const void *source,
					size_t size);

static uint64_t (* const __attribute__((used))
keep_swmmu_load)(const void *, size_t) = nommu_swmmu_load_u64;

static long (* const __attribute__((used))
keep_swmmu_store)(void *, size_t, uint64_t) = nommu_swmmu_store_u64;

static uint64_t (* const __attribute__((used))
keep_swmmu_load_dynamic)(const void *, size_t) =
	nommu_swmmu_load_dynamic;

static long (* const __attribute__((used))
keep_swmmu_store_dynamic)(void *, size_t, uint64_t) =
	nommu_swmmu_store_dynamic;

static void *(* const __attribute__((used))
keep_swmmu_memcpy_dynamic)(void *, const void *, size_t) =
	nommu_swmmu_memcpy_dynamic;
static void *(* const __attribute__((used))
keep_swmmu_memset_dynamic)(void *, int, size_t) =
	nommu_swmmu_memset_dynamic;
static void *(* const __attribute__((used))
keep_swmmu_memmove_dynamic)(void *, const void *, size_t) =
	nommu_swmmu_memmove_dynamic;


extern uint64_t nommu_swmmu_load_u64(const void *address, size_t size);
extern long nommu_swmmu_store_u64(void *address,
				  size_t size,
				  uint64_t value);

extern uint64_t nommu_swmmu_load_dynamic(const void *address,
					 size_t size);

extern long nommu_swmmu_store_dynamic(void *address,
				      size_t size,
				      uint64_t value);
extern void *nommu_swmmu_memcpy(void *destination,
				const void *source,
				size_t size);

extern void *nommu_swmmu_memmove(void *destination,
				 const void *source,
				 size_t size);

extern void *nommu_swmmu_memset(void *destination,
				int value,
				size_t size);

extern void *nommu_swmmu_memcpy_dynamic(
	void *destination,
	const void *source,
	size_t size);
void *nommu_swmmu_memset_dynamic(void *destination,
				int value,
				size_t size);
void *nommu_swmmu_memmove_dynamic(void *destination,
				const void *source,
				size_t size);

