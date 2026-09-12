#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern uint64_t nommu_swmmu_load_u64(const void *address, size_t size);
extern long nommu_swmmu_store_u64(void *address,
				  size_t size,
				  uint64_t value);

extern uint64_t nommu_swmmu_load_dynamic(const void *address,
					 size_t size);

extern long nommu_swmmu_store_dynamic(void *address,
				      size_t size,
				      uint64_t value);

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

extern void *nommu_swmmu_memcpy(void *dst,
				const void *src,
				size_t size);
extern void *nommu_swmmu_memmove(void *dst,
				const void *src,
				size_t size);
extern void *nommu_swmmu_memset(void *dst,
				int b,
				size_t size);

static void *(* const __attribute__((used))
keep_swmmu_memcpy)(void *, const void *, size_t) =
	nommu_swmmu_memcpy;
static void *(* const __attribute__((used))
keep_swmmu_memmove)(void *, const void *, size_t) =
	nommu_swmmu_memmove;
static void *(* const __attribute__((used))
keep_swmmu_memset)(void *, int, size_t) =
	nommu_swmmu_memset;


extern void *contract_memcpy(void *dst,
			     const void *src,
			     size_t size)
	__attribute__((swmmu_memop("memcpy")));

__attribute__((noinline))
void swmmu_contract_memcpy(swmmu_u64_ptr dst,
			   swmmu_u64_ptr src,
			   size_t size)
{
	contract_memcpy(dst, src, size);
}

extern void *contract_memmove(void *dst,
			     const void *src,
			     size_t size)
	__attribute__((swmmu_memop("memmove")));

__attribute__((noinline))
void swmmu_contract_memmove(swmmu_u64_ptr dst,
			   swmmu_u64_ptr src,
			   size_t size)
{
	contract_memmove(dst, src, size);
}

extern void *contract_memset(void *dst,
			     int b,
			     size_t size)
	__attribute__((swmmu_memop("memset")));

__attribute__((noinline))
void swmmu_contract_memset(swmmu_u64_ptr dst,
			   int b,
			   size_t size)
{
	contract_memset(dst, b, size);
}

extern void *contract_malloc(size_t size)
	__attribute__((swmmu_allocator_result));

__attribute__((noinline))
uint64_t load_from_contract_malloc(void)
{
	swmmu_u64_ptr pointer;

	pointer = contract_malloc(sizeof(uint64_t));

	return *pointer;
}

__attribute__((noinline))
uint64_t load_from_contract_malloc_alias(void)
{
	swmmu_u64_ptr pointer;
	swmmu_u64_ptr alias;

	pointer = contract_malloc(sizeof(uint64_t));
	alias = pointer;

	return *alias;
}

extern uint64_t *ordinary_pointer(void);

__attribute__((noinline))
uint64_t dynamic_or_ordinary_load(swmmu_u64_ptr context,
				  uint64_t *ordinary,
				  int condition)
{
	swmmu_u64_ptr dynamic;
	uint64_t *selected;

	(void)context;

	dynamic = contract_malloc(sizeof(uint64_t));

	selected = condition ?
		(uint64_t *)dynamic : ordinary;

	return *selected;
}
