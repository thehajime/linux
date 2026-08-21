// mm/nommu_swmmu_kunit.c

#include <kunit/test.h>
#include <linux/init.h>
#include <linux/nommu_swmmu.h>
#include <linux/stddef.h>
#include <linux/types.h>

struct swmmu_test_object {
	u64 value;
};

static u64 (* const __used swmmu_load_ref)
	(const void *, size_t) = nommu_swmmu_load_u64;

static void (* const __used swmmu_store_ref)
	(void *, size_t, u64) = nommu_swmmu_store_u64;

__attribute__((swmmu))
static noinline u64
test_swmmu_increment(struct swmmu_test_object *object)
{
	object->value++;
	return object->value;
}

static void
nommu_swmmu_increment_test(struct kunit *test)
{
	struct nommu_swmmu_space *space;
	struct swmmu_test_object *object;
	u64 value;

	space = nommu_swmmu_space_create(NULL, NULL);
	KUNIT_ASSERT_NOT_NULL(test, space);

	nommu_swmmu_kunit_set_space(space);

	object = swmmu_alloc(sizeof(*object));
	KUNIT_ASSERT_NOT_NULL(test, object);

	nommu_swmmu_store_u64(&object->value,
			      sizeof(object->value),
			      41);

	value = test_swmmu_increment(object);

	KUNIT_EXPECT_EQ(test, value, 42ULL);

	nommu_swmmu_kunit_clear_space();
	nommu_swmmu_space_destroy(space);
}

static void
nommu_swmmu_clone_test(struct kunit *test)
{
	struct nommu_swmmu_space *parent;
	struct nommu_swmmu_space *child;
	struct swmmu_test_object *parent_object;
	struct swmmu_test_object *child_object;
	uintptr_t virtual_address;
	u64 value;

	parent = nommu_swmmu_space_create(NULL, NULL);
	KUNIT_ASSERT_NOT_NULL(test, parent);

	nommu_swmmu_kunit_set_space(parent);

	parent_object = swmmu_alloc(sizeof(*parent_object));
	KUNIT_ASSERT_NOT_NULL(test, parent_object);

	nommu_swmmu_store_u64(&parent_object->value,
			      sizeof(parent_object->value),
			      41);

	virtual_address = (uintptr_t)parent_object;

	KUNIT_ASSERT_EQ(test,
			swmmu_clone_space(parent, &child),
			0);

	nommu_swmmu_kunit_set_space(child);

	child_object = (struct swmmu_test_object *)virtual_address;

	value = test_swmmu_increment(child_object);

	KUNIT_EXPECT_EQ(test, value, 42ULL);

	nommu_swmmu_kunit_set_space(parent);

	value = nommu_swmmu_load_u64(&parent_object->value,
				     sizeof(parent_object->value));

	KUNIT_EXPECT_EQ(test, value, 41ULL);

	nommu_swmmu_kunit_clear_space();
	nommu_swmmu_space_destroy(child);
	nommu_swmmu_space_destroy(parent);
}

static struct kunit_case nommu_swmmu_test_cases[] = {
	KUNIT_CASE(nommu_swmmu_increment_test),
	KUNIT_CASE(nommu_swmmu_clone_test),
	{}
};

static struct kunit_suite nommu_swmmu_test_suite = {
	.name = "nommu-swmmu",
	.test_cases = nommu_swmmu_test_cases,
};

kunit_test_suite(nommu_swmmu_test_suite);
MODULE_LICENSE("GPL");
