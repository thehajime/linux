#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include "swmmu_test_runtime.h"

typedef uint64_t * __attribute__((swmmu_ptr)) swmmu_u64_ptr;

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

#elif defined(SWMMU_TEST_MIXED_PHI)

__attribute__((noinline))
uint64_t mixed_phi_load(swmmu_u64_ptr swmmu_pointer,
			uint64_t *ordinary_pointer,
			int condition)
{
	uint64_t *selected;

	selected = condition ?
		(uint64_t *)swmmu_pointer : ordinary_pointer;

	return *selected;
}

#elif defined(SWMMU_TEST_MEMOPS)

#include <string.h>

__attribute__((noinline))
void swmmu_memcpy(swmmu_u64_ptr destination,
		  swmmu_u64_ptr source,
		  size_t size)
{
	memcpy(destination, source, size);
}

__attribute__((noinline))
void swmmu_memmove(swmmu_u64_ptr destination,
		   swmmu_u64_ptr source,
		   size_t size)
{
	memmove(destination, source, size);
}

__attribute__((noinline))
void swmmu_memset(swmmu_u64_ptr destination,
		  int value,
		  size_t size)
{
	memset(destination, value, size);
}

#elif defined(SWMMU_TEST_REALLOC_UNKNOWN)

extern uint64_t *unknown_return(void);

__attribute__((noinline))
uint64_t realloc_unknown_load(swmmu_u64_ptr context)
{
	uint64_t *pointer;
	uint64_t *result;

	(void)context;

	pointer = unknown_return();
	result = realloc(pointer, sizeof(uint64_t));

	return *result;
}
#else

#error "select an SWMMU negative test"

#endif
