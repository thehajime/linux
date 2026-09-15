#include <stddef.h>
#include <stdint.h>

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
keep_swmmu_memcpy)(void *, const void *, size_t) =
	nommu_swmmu_memcpy;

static void *(* const __attribute__((used))
keep_swmmu_memmove)(void *, const void *, size_t) =
	nommu_swmmu_memmove;

static void *(* const __attribute__((used))
keep_swmmu_memset)(void *, int, size_t) =
	nommu_swmmu_memset;


__attribute__((noinline))
uint64_t
swmmu_all_access_load(const uint64_t *pointer)
{
	return *pointer;
}

__attribute__((noinline))
void
swmmu_all_access_store(uint64_t *pointer, uint64_t value)
{
	*pointer = value;
}

struct swmmu_all_access_record {
	uint64_t value;
};

__attribute__((noinline))
uint64_t
swmmu_all_access_field(
	const struct swmmu_all_access_record *record)
{
	return record->value;
}

__attribute__((noinline))
void
swmmu_all_access_array(uint64_t *array,
		       size_t index,
		       uint64_t value)
{
	array[index] = value;
}

__attribute__((noinline, noipa, used))
uint64_t
swmmu_all_access_alias_load(const uint64_t *pointer)
{
	const uint64_t *alias = pointer;

	return *alias;
}

__attribute__((noinline, noipa, used))
void
swmmu_all_access_alias_store(uint64_t *pointer, uint64_t value)
{
	uint64_t *alias = pointer;

	*alias = value;
}

struct swmmu_all_access_holder {
	uint64_t *pointer;
};

__attribute__((noinline, noipa, used))
void
swmmu_all_access_store_field(
	struct swmmu_all_access_holder *holder,
	uint64_t *pointer)
{
	holder->pointer = pointer;
}

__attribute__((noinline, noipa, used))
uint64_t
swmmu_all_access_load_field(
	const struct swmmu_all_access_holder *holder)
{
	return *holder->pointer;
}

__attribute__((noinline, noipa, used))
uint64_t
swmmu_all_access_cross_function(uint64_t *pointer)
{
	struct swmmu_all_access_holder holder;

	swmmu_all_access_store_field(&holder, pointer);
	return swmmu_all_access_load_field(&holder);
}

extern uint64_t *swmmu_all_access_unknown_pointer(void);

__attribute__((noinline, noipa, used))
uint64_t
swmmu_all_access_unknown_load(void)
{
	return *swmmu_all_access_unknown_pointer();
}

__attribute__((noinline, noipa, used))
void
swmmu_all_access_unknown_store(uint64_t value)
{
	*swmmu_all_access_unknown_pointer() = value;
}

extern void *memcpy(void *destination,
		    const void *source,
		    size_t size);

extern void *memmove(void *destination,
		     const void *source,
		     size_t size);

extern void *memset(void *destination,
		    int value,
		    size_t size);

__attribute__((noinline, noipa, used))
void
swmmu_all_access_memcpy(void *destination,
			const void *source,
			size_t size)
{
	memcpy(destination, source, size);
}

__attribute__((noinline, noipa, used))
void
swmmu_all_access_memmove(void *destination,
			 const void *source,
			 size_t size)
{
	memmove(destination, source, size);
}

__attribute__((noinline, noipa, used))
void
swmmu_all_access_memset(void *destination,
			int value,
			size_t size)
{
	memset(destination, value, size);
}
