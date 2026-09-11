#include <stdint.h>
#include <stddef.h>

typedef uint64_t * __attribute__((swmmu_ptr)) swmmu_u64_ptr;

extern uint64_t nommu_swmmu_load_u64(const void *address, size_t size);
extern long nommu_swmmu_store_u64(void *address,
				size_t size,
				uint64_t value);

static uint64_t (* const __attribute__((used))
keep_swmmu_load)(const void *, size_t) = nommu_swmmu_load_u64;

static long (* const __attribute__((used))
keep_swmmu_store)(void *, size_t, uint64_t) = nommu_swmmu_store_u64;

#if defined(SWMMU_TEST_UNKNOWN_BOUNDARY)

extern uint64_t *ordinary_return(uint64_t *);

__attribute__((noinline))
uint64_t unknown_boundary_load(swmmu_u64_ptr pointer)
{
	uint64_t *ordinary_pointer;

	ordinary_pointer = ordinary_return((uint64_t *)pointer);

	return *ordinary_pointer;
}

#elif defined(SWMMU_TEST_UNKNOWN_RETURN)

extern uint64_t *unknown_return(void);

__attribute__((noinline))
uint64_t unknown_return_load(swmmu_u64_ptr context)
{
	uint64_t *pointer;

	(void)context;
	pointer = unknown_return();

	return *pointer;
}

#else

#error "select an SWMMU negative test"

#endif
