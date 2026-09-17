#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "swmmu_test_runtime.h"

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

__attribute__((noinline))
void store_to_contract_malloc(swmmu_u64_ptr context,
			       uint64_t value)
{
	swmmu_u64_ptr pointer;

	(void)context;

	pointer = contract_malloc(sizeof(uint64_t));
	*pointer = value;
}

__attribute__((noinline))
uint64_t load_from_malloc(void)
{
	uint64_t *pointer;

	pointer = malloc(sizeof(*pointer));
	return *pointer;
}

__attribute__((noinline))
void store_to_malloc(uint64_t value)
{
	uint64_t *pointer;

	pointer = malloc(sizeof(*pointer));
	*pointer = value;
}

__attribute__((noinline))
uint64_t load_from_calloc(void)
{
	uint64_t *pointer;

	pointer = calloc(1, sizeof(*pointer));
	return *pointer;
}

__attribute__((noinline))
uint64_t load_from_realloc(void)
{
	uint64_t *pointer;

	pointer = malloc(sizeof(*pointer));
	pointer = realloc(pointer, sizeof(*pointer));

	return *pointer;
}

__attribute__((noinline))
uint64_t load_from_realloc_swmmu(swmmu_u64_ptr pointer)
{
	uint64_t *result;

	result = realloc((uint64_t *)pointer, sizeof(uint64_t));

	return *result;
}

__attribute__((noinline))
uint64_t load_from_realloc_ordinary(uint64_t *pointer)
{
	uint64_t *result;

	result = realloc(pointer, sizeof(uint64_t));

	return *result;
}

struct svm_plain_record {
	uint64_t value;
};

__attribute__((noinline, swmmu))
uint64_t svm_plain_pointer_load(const uint64_t *pointer)
{
	return *pointer;
}

__attribute__((noinline, swmmu))
void svm_plain_pointer_store(uint64_t *pointer, uint64_t value)
{
	*pointer = value;
}

__attribute__((noinline, swmmu))
uint64_t svm_plain_field_load(const struct svm_plain_record *record)
{
	return record->value;
}

__attribute__((noinline, swmmu))
void svm_plain_field_store(struct svm_plain_record *record, uint64_t value)
{
	record->value = value;
}

__attribute__((noinline, swmmu))
uint64_t svm_plain_array_load(const uint64_t *array, size_t index)
{
	return array[index];
}

__attribute__((noinline, swmmu))
void svm_plain_array_store(uint64_t *array, size_t index, uint64_t value)
{
	array[index] = value;
}

__attribute__((noinline, swmmu))
uint64_t svm_plain_cross_function_load(const uint64_t *pointer)
{
	uint64_t value;

	value = svm_plain_pointer_load(pointer);
	return value + 1;
}

struct svm_cross_holder {
	uint64_t *pointer;
};

__attribute__((swmmu, noinline))
static void
svm_cross_store(struct svm_cross_holder *holder, uint64_t *pointer)
{
	holder->pointer = pointer;
}

__attribute__((swmmu, noinline))
static uint64_t
svm_cross_load(const struct svm_cross_holder *holder)
{
	return *holder->pointer;
}

__attribute__((swmmu, noinline))
uint64_t
svm_cross_function_field_load(uint64_t *pointer)
{
	struct svm_cross_holder holder;

	svm_cross_store(&holder, pointer);
	return svm_cross_load(&holder);
}

extern uint64_t *svm_unknown_pointer(void);

__attribute__((noinline, swmmu))
uint64_t
svm_unknown_return_load(void)
{
	return *svm_unknown_pointer();
}

__attribute__((noinline, swmmu))
void
svm_unknown_return_store(uint64_t value)
{
	*svm_unknown_pointer() = value;
}
