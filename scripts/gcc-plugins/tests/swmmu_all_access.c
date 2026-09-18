#include <stddef.h>
#include <stdint.h>

#include "swmmu_test_runtime.h"

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

struct swmmu_all_access_pair {
	uint64_t first;
	uint64_t second;
};

__attribute__((noinline, noipa, used))
void
swmmu_all_access_struct_copy(
	struct swmmu_all_access_pair *destination,
	const struct swmmu_all_access_pair *source)
{
	*destination = *source;
}

__attribute__((noinline, noipa, used))
void
swmmu_all_access_struct_field_copy(
	struct swmmu_all_access_pair *destination,
	const struct swmmu_all_access_pair *source)
{
	destination->first = source->first;
	destination->second = source->second;
}

struct swmmu_all_access_bits {
	uint64_t first:5;
	uint64_t second:1;
	uint64_t third:6;
	uint64_t fourth:52;
};

__attribute__((noinline, noipa, used))
void
swmmu_all_access_bitfield_store(
	struct swmmu_all_access_bits *bits,
	uint64_t first,
	uint64_t second,
	uint64_t third,
	uint64_t fourth)
{
	bits->first = first;
	bits->second = second;
	bits->third = third;
	bits->fourth = fourth;
}

__attribute__((noinline, noipa, used))
uint64_t
swmmu_all_access_bitfield_load(
	const struct swmmu_all_access_bits *bits)
{
	return bits->first |
		(bits->second << 5) |
		(bits->third << 6) |
		(bits->fourth << 12);
}

struct swmmu_all_access_nested {
	struct {
		struct {
			uint32_t address;
		} sin;
	} u;
};

__attribute__((noinline, noipa, used))
void
swmmu_all_access_nested_store(
	struct swmmu_all_access_nested *record,
	uint32_t value)
{
	record->u.sin.address = value;
}

typedef enum {
	SWMMU_TEST_STREAM_FILE,
	SWMMU_TEST_STREAM_STRING,
	SWMMU_TEST_STREAM_BUFFERED,
} swmmu_test_stream_type;

typedef union {
	void *file;
	char *string;
	int buffered_fd;
} swmmu_test_input_stream;

typedef int (*swmmu_test_getter)(void);
typedef int (*swmmu_test_ungetter)(int);

struct swmmu_all_access_input {
	swmmu_test_stream_type type;
	char *name;
	swmmu_test_input_stream location;
	swmmu_test_getter getter;
	swmmu_test_ungetter ungetter;
};

struct swmmu_all_access_saver {
	struct swmmu_all_access_saver *next;
	struct swmmu_all_access_input input;
};

extern void
swmmu_all_access_init_input(
	swmmu_test_getter getter,
	swmmu_test_ungetter ungetter,
	swmmu_test_stream_type type,
	char *name,
	swmmu_test_input_stream location);

__attribute__((noinline, noipa, used))
void
swmmu_all_access_aggregate_call_argument(
	struct swmmu_all_access_saver *saver)
{
	swmmu_all_access_init_input(
		saver->input.getter,
		saver->input.ungetter,
		saver->input.type,
		saver->input.name,
		saver->input.location);
}
