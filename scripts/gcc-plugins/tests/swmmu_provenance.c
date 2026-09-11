#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern uint64_t nommu_swmmu_load_u64(const void *address, size_t size);
extern long nommu_swmmu_store_u64(void *address,
				  size_t size,
				  uint64_t value);

static uint64_t (* const __attribute__((used))
keep_swmmu_load)(const void *, size_t) = nommu_swmmu_load_u64;

static long (* const __attribute__((used))
keep_swmmu_store)(void *, size_t, uint64_t) = nommu_swmmu_store_u64;

__attribute__((noinline, swmmu))
uint64_t function_marked_load(uint64_t *p)
{
	return *p;
}

__attribute__((noinline, swmmu))
void function_marked_store(uint64_t *p, uint64_t value)
{
	*p = value;
}

typedef uint64_t * __attribute__((swmmu_ptr)) swmmu_u64_ptr;

__attribute__((noinline))
uint64_t typed_load(swmmu_u64_ptr pointer)
{
	return *pointer;
}

__attribute__((noinline))
uint64_t ordinary_load(uint64_t *pointer)
{
	return *pointer;
}

__attribute__((noinline))
uint64_t typed_alias_load(swmmu_u64_ptr pointer)
{
	swmmu_u64_ptr alias = pointer;

	return *alias;
}

__attribute__((noinline))
void typed_store(swmmu_u64_ptr pointer, uint64_t value)
{
	*pointer = value;
}

__attribute__((noinline))
uint64_t typed_index_load(swmmu_u64_ptr pointer, size_t index)
{
	return pointer[index];
}

__attribute__((noinline))
void typed_index_store(swmmu_u64_ptr pointer,
		       size_t index,
		       uint64_t value)
{
	pointer[index] = value;
}

__attribute__((noinline))
uint64_t typed_cast_load(swmmu_u64_ptr pointer)
{
	return *(uint64_t *)(void *)pointer;
}

__attribute__((noinline))
swmmu_u64_ptr typed_return(swmmu_u64_ptr pointer)
{
	return pointer;
}

__attribute__((noinline))
uint64_t typed_load_through_return(swmmu_u64_ptr pointer)
{
	return *typed_return(pointer);
}

struct typed_holder {
	swmmu_u64_ptr pointer;
};

__attribute__((noinline))
uint64_t typed_struct_load(struct typed_holder *holder)
{
	return *holder->pointer;
}

__attribute__((noinline))
void typed_struct_store(struct typed_holder *holder,
			uint64_t value)
{
	*holder->pointer = value;
}

__attribute__((noinline))
uint64_t typed_pointer_array_load(swmmu_u64_ptr *array,
				  size_t index)
{
	return *array[index];
}

__attribute__((noinline))
swmmu_u64_ptr typed_select(swmmu_u64_ptr first,
			   swmmu_u64_ptr second,
			   int condition)
{
	return condition ? first : second;
}

__attribute__((noinline))
uint64_t typed_phi_load(swmmu_u64_ptr first,
			swmmu_u64_ptr second,
			int condition)
{
	return *typed_select(first, second, condition);
}

__attribute__((noinline))
uint64_t typed_parameter_caller(swmmu_u64_ptr pointer)
{
	return typed_load(pointer);
}

__attribute__((noinline))
uint64_t typed_direct_phi_load(swmmu_u64_ptr first,
			       swmmu_u64_ptr second,
			       int condition)
{
	swmmu_u64_ptr selected;

	selected = condition ? first : second;
	return *selected;
}

__attribute__((noinline))
void ordinary_memcpy(uint8_t *destination,
		     const uint8_t *source,
		     size_t size)
{
	memcpy(destination, source, size);
}

__attribute__((noinline))
void ordinary_memmove(uint8_t *destination,
		      const uint8_t *source,
		      size_t size)
{
	memmove(destination, source, size);
}

__attribute__((noinline))
void ordinary_memset(uint8_t *destination,
		     int value,
		     size_t size)
{
	memset(destination, value, size);
}

extern void *nommu_swmmu_memcpy(void *destination,
				const void *source,
				size_t size);

static void *(* const __attribute__((used))
keep_swmmu_memcpy)(void *, const void *, size_t) =
	nommu_swmmu_memcpy;

extern void *contract_memcpy(void *destination,
			     const void *source,
			     size_t size)
	__attribute__((swmmu_memop("memcpy")));

__attribute__((noinline))
void swmmu_contract_memcpy(swmmu_u64_ptr destination,
			   swmmu_u64_ptr source,
			   size_t size)
{
	contract_memcpy(destination, source, size);
}

