// mm/nommu_swmmu_kunit.c

#include <kunit/test.h>
#include <linux/init.h>
#include <linux/nommu_swmmu.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/highmem.h>

/*
 * note: this macro should be updated once the implementation
 * of swmmu_alloc/clone() will be changed.
 */
#define SWMMU_ALLOC_ZALLOC_POINTS	2
#define SWMMU_ALLOC_TEST_PAGES		4
#define SWMMU_CLONE_ZALLOC_POINTS	3
#define SWMMU_CLONE_TEST_PAGES		4

struct swmmu_test_object {
	u64 value;
};

struct nommu_swmmu_test_ctx {
	struct nommu_swmmu_space *space;
};

enum test_fail_kind {
	TEST_FAIL_NONE,
	TEST_FAIL_ZALLOC,
	TEST_FAIL_PAGE_ALLOC,
	TEST_FAIL_PAGE_COPY,
};

struct test_alloc_state {
	enum test_fail_kind kind;
	unsigned int fail_at;
	unsigned int count;

	unsigned int zalloc_success;
	unsigned int dealloc_count;
	unsigned int page_alloc_success;
	unsigned int page_free_count;
	unsigned int page_copy_count;
};

static struct test_alloc_state test_alloc_state;

static void test_alloc_reset(void)
{
	memset(&test_alloc_state, 0, sizeof(test_alloc_state));
	test_alloc_state.kind = TEST_FAIL_NONE;
}

static void test_alloc_fail_at(enum test_fail_kind kind,
			       unsigned int fail_at)
{
	test_alloc_reset();

	test_alloc_state.kind = kind;
	test_alloc_state.fail_at = fail_at;
}

static bool test_should_fail(enum test_fail_kind kind)
{
	if (test_alloc_state.kind != kind)
		return false;

	return test_alloc_state.count++ ==
	       test_alloc_state.fail_at;
}


static inline void *test_kzalloc(size_t size, gfp_t gfp)
{
	void *ptr;

	if (test_should_fail(TEST_FAIL_ZALLOC))
		return NULL;

	ptr = kzalloc(size, gfp);
	if (ptr)
		test_alloc_state.zalloc_success++;

	return ptr;
}

static inline void test_kfree(void *ptr)
{
	if (ptr)
		test_alloc_state.dealloc_count++;

	kfree(ptr);
}

static inline struct page *test_alloc_page(gfp_t gfp)
{
	struct page *page;

	if (test_should_fail(TEST_FAIL_PAGE_ALLOC))
		return NULL;

	page = alloc_page(gfp);
	if (page)
		test_alloc_state.page_alloc_success++;

	return page;
}

static inline void test_free_page(struct page *page)
{
	if (page)
		test_alloc_state.page_free_count++;

	__free_page(page);
}

static inline int test_copy_page(struct page *dst,
				const struct page *src)
{
	test_alloc_state.page_copy_count++;

	if (test_should_fail(TEST_FAIL_PAGE_COPY))
		return -EIO;

	copy_highpage(dst, (struct page *)src);
	return 0;
}

static const struct nommu_swmmu_mem_ops test_ops = {
	.zalloc = test_kzalloc,
	.dealloc = test_kfree,
	.page_alloc = test_alloc_page,
	.page_free = test_free_page,
	.page_copy = test_copy_page,
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

	space = nommu_swmmu_space_create_with_ops(&test_ops);
	KUNIT_ASSERT_NOT_NULL(test, space);

	nommu_swmmu_kunit_set_space(space);

	object = (void *)swmmu_alloc(sizeof(*object));
	KUNIT_ASSERT_GT(test, (long)object, 0L);

	nommu_swmmu_store_u64(&object->value,
			      sizeof(object->value),
			      41);

	value = test_swmmu_increment(object);

	KUNIT_EXPECT_EQ(test, value, 42ULL);

	nommu_swmmu_kunit_clear_space();
	nommu_swmmu_space_put(space);
}

static void
destroy_test_space(struct nommu_swmmu_space *space)
{
	nommu_swmmu_kunit_clear_space();
	nommu_swmmu_space_put(space);
}

KUNIT_DEFINE_ACTION_WRAPPER(
	destroy_test_space_action,
	destroy_test_space,
	struct nommu_swmmu_space *);

static void
nommu_swmmu_clone_test(struct kunit *test)
{
	struct nommu_swmmu_space *parent;
	struct nommu_swmmu_space *child;
	struct swmmu_test_object *parent_object1;
	struct swmmu_test_object *parent_object2;
	struct swmmu_test_object *parent_object3;
	struct swmmu_test_object *child_object1;
	struct swmmu_test_object *child_object2;
	struct swmmu_test_object *child_object3;
	uintptr_t vaddr1, vaddr2, vaddr3;
	u64 value;
	int ret;

	parent = nommu_swmmu_space_create_with_ops(&test_ops);
	KUNIT_ASSERT_NOT_NULL(test, parent);
	ret = kunit_add_action_or_reset(test,
					destroy_test_space_action, parent);
	KUNIT_ASSERT_EQ(test, ret, 0);

	nommu_swmmu_kunit_set_space(parent);

	parent_object1 = (void *)swmmu_alloc(sizeof(*parent_object1));
	KUNIT_ASSERT_GT(test, (long)parent_object1, 0L);

	parent_object2 = (void *)swmmu_alloc(SWMMU_PAGE_SIZE * 2);
	KUNIT_ASSERT_GT(test, (long)parent_object2, 0L);

	nommu_swmmu_store_u64(&parent_object1->value,
			      sizeof(parent_object1->value),
			      41);

	nommu_swmmu_store_u64(&parent_object2->value,
			      sizeof(parent_object2->value),
			      81);

	parent_object3 = (void *)((uintptr_t)parent_object2 +
				SWMMU_PAGE_SIZE);
	nommu_swmmu_store_u64(&parent_object3->value,
			      sizeof(parent_object3->value),
			      91);

	vaddr1 = (uintptr_t)parent_object1;
	vaddr2 = (uintptr_t)parent_object2;
	vaddr3 = (uintptr_t)parent_object3;

	KUNIT_ASSERT_EQ(test,
			swmmu_clone_space(parent, &child),
			0);

	nommu_swmmu_kunit_set_space(child);

	ret = kunit_add_action_or_reset(test,
					destroy_test_space_action, child);
	KUNIT_ASSERT_EQ(test, ret, 0);

	child_object1 = (struct swmmu_test_object *)vaddr1;
	child_object2 = (struct swmmu_test_object *)vaddr2;
	child_object3 = (struct swmmu_test_object *)vaddr3;

	kunit_log(KERN_INFO, test, "# %s: parent=%p, child=%p", __func__,
		parent_object1, child_object1);
	kunit_log(KERN_INFO, test, "# %s: parent=%p, child=%p", __func__,
		parent_object2, child_object2);

	value = test_swmmu_increment(child_object1);
	KUNIT_EXPECT_EQ(test, value, 42ULL);
	kunit_log(KERN_INFO, test, "# %s: child=%llu", __func__, value);

	value = test_swmmu_increment(child_object2);
	KUNIT_EXPECT_EQ(test, value, 82ULL);
	kunit_log(KERN_INFO, test, "# %s: child=%llu", __func__, value);

	value = test_swmmu_increment(child_object3);
	KUNIT_EXPECT_EQ(test, value, 92ULL);
	kunit_log(KERN_INFO, test, "# %s: child=%llu", __func__, value);

	nommu_swmmu_kunit_set_space(parent);

	value = nommu_swmmu_load_u64(&parent_object1->value,
				     sizeof(parent_object1->value));
	KUNIT_EXPECT_EQ(test, value, 41ULL);
	kunit_log(KERN_INFO, test, "# %s: parent=%llu", __func__, value);

	value = nommu_swmmu_load_u64(&parent_object2->value,
				     sizeof(parent_object2->value));
	KUNIT_EXPECT_EQ(test, value, 81ULL);
	kunit_log(KERN_INFO, test, "# %s: parent=%llu", __func__, value);

	value = nommu_swmmu_load_u64(&parent_object3->value,
				     sizeof(parent_object3->value));
	KUNIT_EXPECT_EQ(test, value, 91ULL);
	kunit_log(KERN_INFO, test, "# %s: parent=%llu", __func__, value);
}

static void nommu_swmmu_attach_test(struct kunit *test)
{
	struct nommu_swmmu_space *space;
	struct nommu_swmmu_space *old_space;
	struct mm_struct *test_mm;
	int ret;

	space = nommu_swmmu_space_create_with_ops(&test_ops);
	KUNIT_ASSERT_NOT_NULL(test, space);

	test_mm = mm_alloc();
	KUNIT_ASSERT_NOT_NULL(test, test_mm);

	old_space = test_mm->swmmu_space;
	KUNIT_ASSERT_NOT_NULL(test, old_space);

	/*
	 * mm_alloc() creates and attaches an initial SWMMU space.
	 * Remove that mm-owned reference before testing attach().
	 */
	nommu_swmmu_space_detach(test_mm);

	/*
	 * Test the attach helper directly. Do not rely on
	 * nommu_swmmu_space_create_with_ops(&test_ops) implicitly using current->mm.
	 */
	ret = nommu_swmmu_space_attach(test_mm, space);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_PTR_EQ(test, test_mm->swmmu_space, space);

	nommu_swmmu_space_detach(test_mm);
	mmput(test_mm);
}

static void nommu_swmmu_cross_page_test(struct kunit *test)
{
	void *base;
	void *address;
	u64 value;

	base = (void *)swmmu_alloc(SWMMU_PAGE_SIZE * 2);
	KUNIT_ASSERT_GT(test, (long)base, 0L);

	address = (void *)((uintptr_t)base +
			   SWMMU_PAGE_SIZE - sizeof(u32));

	nommu_swmmu_store_u64(address, sizeof(u64),
			      0x1122334455667788ULL);

	value = nommu_swmmu_load_u64(address, sizeof(u64));

	KUNIT_EXPECT_EQ(test, value, 0x1122334455667788ULL);

	KUNIT_EXPECT_EQ(test, swmmu_free(base), 0);
}

static void nommu_swmmu_translate_boundary_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	void *base;

	base = (void *)swmmu_alloc(SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_GT(test, (long)base, 0L);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)base,
						sizeof(u64),
						0),
			0);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)base +
						SWMMU_PAGE_SIZE -
						sizeof(u32),
						sizeof(u64),
						0),
			-EFAULT);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)base +
						SWMMU_PAGE_SIZE,
						sizeof(u64),
						0),
			-EFAULT);

	KUNIT_EXPECT_EQ(test, swmmu_free(base), 0);
}

static void nommu_swmmu_multiple_mapping_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	void *first;
	void *second;
	u64 value;

	first = (void *)swmmu_alloc(SWMMU_PAGE_SIZE);
	second = (void *)swmmu_alloc(SWMMU_PAGE_SIZE * 2);

	KUNIT_ASSERT_GT(test, (long)first, 0L);
	KUNIT_ASSERT_GT(test, (long)second, 0L);

	KUNIT_EXPECT_PTR_NE(test, first, second);

	nommu_swmmu_store_u64(first, sizeof(u64), 11);
	nommu_swmmu_store_u64(second, sizeof(u64), 22);

	value = nommu_swmmu_load_u64(first, sizeof(u64));
	KUNIT_EXPECT_EQ(test, value, 11ULL);

	value = nommu_swmmu_load_u64(second, sizeof(u64));
	KUNIT_EXPECT_EQ(test, value, 22ULL);

	KUNIT_EXPECT_EQ(test, swmmu_free(first), 0);
	KUNIT_EXPECT_EQ(test, swmmu_free(second), 0);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)first,
						sizeof(u64),
						0),
			-EFAULT);
}

static void nommu_swmmu_free_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	void *base;

	base = (void *)swmmu_alloc(SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_GT(test, (long)base, 0L);

	KUNIT_EXPECT_EQ(test, swmmu_free(base), 0);

	KUNIT_EXPECT_EQ(test, swmmu_free(base), -EINVAL);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)base,
						sizeof(u64),
						0),
			-EFAULT);
}

static void nommu_swmmu_free_interior_test(struct kunit *test)
{
	void *base;
	void *interior;

	base = (void *)swmmu_alloc(SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_GT(test, (long)base, 0L);

	interior = (void *)((uintptr_t)base + sizeof(u64));

	KUNIT_EXPECT_EQ(test, swmmu_free(interior), -EINVAL);
	KUNIT_EXPECT_EQ(test, swmmu_free(base), 0);
}

static void nommu_swmmu_repeated_alloc_free_test(struct kunit *test)
{
	int i;

	for (i = 0; i < 128; i++) {
		void *base;

		base = (void *)swmmu_alloc((i % 4 + 1) * SWMMU_PAGE_SIZE);
		KUNIT_ASSERT_GT(test, (long)base, 0L);

		nommu_swmmu_store_u64(base, sizeof(u64), i);

		KUNIT_EXPECT_EQ(test,
				nommu_swmmu_load_u64(base, sizeof(u64)),
				(u64)i);

		KUNIT_ASSERT_EQ(test, swmmu_free(base), 0);
	}
}

static void nommu_swmmu_repeated_clone_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	int i;

	for (i = 0; i < 32; i++) {
		struct nommu_swmmu_space *child;
		void *base;
		u64 value;

		base = (void *)swmmu_alloc(SWMMU_PAGE_SIZE * 2);
		KUNIT_ASSERT_NOT_NULL(test, base);

		nommu_swmmu_store_u64(base, sizeof(u64), 100 + i);

		KUNIT_ASSERT_EQ(test,
				swmmu_clone_space(ctx->space, &child),
				0);

		nommu_swmmu_kunit_set_space(child);

		value = nommu_swmmu_load_u64(base, sizeof(u64));
		KUNIT_EXPECT_EQ(test, value, (u64)(100 + i));

		nommu_swmmu_kunit_clear_space();
		nommu_swmmu_space_put(child);

		nommu_swmmu_kunit_set_space(ctx->space);

		KUNIT_ASSERT_EQ(test, swmmu_free(base), 0);
	}
}

static void
expect_allocations_balanced(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test,
			test_alloc_state.zalloc_success,
			test_alloc_state.dealloc_count);

	KUNIT_EXPECT_EQ(test,
			test_alloc_state.page_alloc_success,
			test_alloc_state.page_free_count);
}

static void nommu_swmmu_alloc_failure_test(struct kunit *test)
{
	struct nommu_swmmu_space *space;
	long ret;
	int fail_at;

	test_alloc_reset();

	space = nommu_swmmu_space_create_with_ops(&test_ops);
	KUNIT_ASSERT_NOT_NULL(test, space);

	nommu_swmmu_kunit_set_space(space);

	for (fail_at = 0; fail_at < SWMMU_ALLOC_ZALLOC_POINTS; fail_at++) {
		test_alloc_fail_at(TEST_FAIL_ZALLOC, fail_at);

		ret = swmmu_alloc(SWMMU_PAGE_SIZE);
		KUNIT_EXPECT_LT(test, ret, 0L);
		expect_allocations_balanced(test);

		test_alloc_reset();

		ret = swmmu_alloc(SWMMU_PAGE_SIZE);
		KUNIT_ASSERT_GT(test, ret, 0L);

		KUNIT_EXPECT_EQ(test,
				swmmu_free((void *)(uintptr_t)ret),
				0);
		expect_allocations_balanced(test);
	}

	nommu_swmmu_kunit_clear_space();
	nommu_swmmu_space_put(space);
}

static void nommu_swmmu_alloc_page_failure_test(struct kunit *test)
{
	struct nommu_swmmu_space *space;
	long ret;
	unsigned int fail_at;

	space = nommu_swmmu_space_create_with_ops(&test_ops);
	KUNIT_ASSERT_NOT_NULL(test, space);

	nommu_swmmu_kunit_set_space(space);

	for (fail_at = 0;
	     fail_at < SWMMU_ALLOC_TEST_PAGES;
	     fail_at++) {
		test_alloc_fail_at(TEST_FAIL_PAGE_ALLOC, fail_at);

		ret = swmmu_alloc(SWMMU_ALLOC_TEST_PAGES *
				  SWMMU_PAGE_SIZE);
		KUNIT_EXPECT_LT(test, ret, 0L);
		expect_allocations_balanced(test);

		test_alloc_reset();

		ret = swmmu_alloc(SWMMU_ALLOC_TEST_PAGES *
				  SWMMU_PAGE_SIZE);
		KUNIT_ASSERT_GT(test, ret, 0L);

		KUNIT_ASSERT_EQ(test,
				swmmu_free((void *)(uintptr_t)ret),
				0);
		expect_allocations_balanced(test);
	}

	nommu_swmmu_kunit_clear_space();
	nommu_swmmu_space_put(space);
}

static void nommu_swmmu_clone_failure_test(struct kunit *test)
{
	struct nommu_swmmu_space *space, *child;
	long ret;
	int fail_at;
	void *base;

	space = nommu_swmmu_space_create_with_ops(&test_ops);
	KUNIT_ASSERT_NOT_NULL(test, space);
	nommu_swmmu_kunit_set_space(space);

	ret = swmmu_alloc(SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_GT(test, ret, 0L);

	base = (void *)(uintptr_t)ret;
	nommu_swmmu_store_u64(base, sizeof(u64), 41);

	test_alloc_reset();

	for (fail_at = 0; fail_at < SWMMU_CLONE_ZALLOC_POINTS; fail_at++) {
		test_alloc_fail_at(TEST_FAIL_ZALLOC, fail_at);

		child = NULL;
		ret = swmmu_clone_space(space, &child);

		KUNIT_EXPECT_LT(test, ret, 0L);
		KUNIT_EXPECT_NULL(test, child);
		expect_allocations_balanced(test);
		KUNIT_EXPECT_EQ(test,
			nommu_swmmu_load_u64(base, sizeof(u64)),
			41ULL);

		test_alloc_reset();

		ret = swmmu_clone_space(space, &child);
		KUNIT_ASSERT_EQ(test, ret, 0L);

		nommu_swmmu_space_put(child);
		expect_allocations_balanced(test);
	}

	nommu_swmmu_kunit_clear_space();
	nommu_swmmu_space_put(space);
}

static void nommu_swmmu_clone_page_failure_test(struct kunit *test)
{
	struct nommu_swmmu_space *space, *child = NULL;
	long ret;
	unsigned int fail_at;
	void *base;

	space = nommu_swmmu_space_create_with_ops(&test_ops);
	KUNIT_ASSERT_NOT_NULL(test, space);

	nommu_swmmu_kunit_set_space(space);

	ret = swmmu_alloc(SWMMU_CLONE_TEST_PAGES * SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_GT(test, ret, 0L);

	base = (void *)(uintptr_t)ret;
	for (int i = 0; i < SWMMU_CLONE_TEST_PAGES; i++) {
		void *page_base = (void *)((uintptr_t)base +
					i * SWMMU_PAGE_SIZE);

		nommu_swmmu_store_u64(page_base,
				sizeof(u64),
				100 + i);
	}

	test_alloc_reset();

	for (fail_at = 0;
	     fail_at < SWMMU_CLONE_TEST_PAGES;
	     fail_at++) {
		test_alloc_fail_at(TEST_FAIL_PAGE_ALLOC, fail_at);

		ret = swmmu_clone_space(space, &child);
		KUNIT_EXPECT_LT(test, ret, 0L);
		KUNIT_EXPECT_NULL(test, child);
		expect_allocations_balanced(test);

		KUNIT_EXPECT_EQ(test,
				nommu_swmmu_load_u64(base, sizeof(u64)),
				100ULL);

		KUNIT_EXPECT_EQ(test,
				nommu_swmmu_load_u64(
					(void *)((uintptr_t)base +
						(SWMMU_CLONE_TEST_PAGES - 1) *
						SWMMU_PAGE_SIZE),
					sizeof(u64)),
				100ULL + SWMMU_CLONE_TEST_PAGES - 1);

		test_alloc_reset();

		ret = swmmu_clone_space(space, &child);
		KUNIT_ASSERT_EQ(test, ret, 0L);

		nommu_swmmu_space_put(child);
		expect_allocations_balanced(test);
	}

	nommu_swmmu_kunit_clear_space();
	nommu_swmmu_space_put(space);
}

static void nommu_swmmu_clone_copy_failure_test(struct kunit *test)
{
	struct nommu_swmmu_space *space;
	struct nommu_swmmu_space *child = NULL;
	void *base;
	long ret;
	unsigned int fail_at;

	space = nommu_swmmu_space_create_with_ops(&test_ops);
	KUNIT_ASSERT_NOT_NULL(test, space);

	nommu_swmmu_kunit_set_space(space);

	ret = swmmu_alloc(SWMMU_CLONE_TEST_PAGES *
			  SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_GT(test, ret, 0L);

	base = (void *)(uintptr_t)ret;
	nommu_swmmu_store_u64(base, sizeof(u64), 41);

	for (fail_at = 0;
	     fail_at < SWMMU_CLONE_TEST_PAGES;
	     fail_at++) {
		test_alloc_reset();
		test_alloc_fail_at(TEST_FAIL_PAGE_COPY, fail_at);

		child = NULL;
		ret = swmmu_clone_space(space, &child);

		KUNIT_EXPECT_LT(test, ret, 0L);
		KUNIT_EXPECT_NULL(test, child);
		expect_allocations_balanced(test);

		KUNIT_EXPECT_EQ(test,
				nommu_swmmu_load_u64(base, sizeof(u64)),
				41ULL);
	}

	test_alloc_reset();
	nommu_swmmu_kunit_clear_space();
	nommu_swmmu_space_put(space);
}

/* map API tests */
static void nommu_swmmu_map_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	long ret;

	/* a one-byte request is rounded to one page */
	ret = nommu_swmmu_map(ctx->space, 1);
	KUNIT_ASSERT_GT(test, ret, 0L);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)ret +
						SWMMU_PAGE_SIZE - 1,
						1,
						0),
			0);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)ret +
						SWMMU_PAGE_SIZE,
						1,
						0),
			-EFAULT);

	/* mapping is accessible */
	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)ret,
						sizeof(u64),
						0),
			0);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_unmap(ctx->space,
					(unsigned long)ret,
					SWMMU_PAGE_SIZE),
			0);

}

static void nommu_swmmu_unmap_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	long ret, addr;
	void *base;

	addr = nommu_swmmu_map(ctx->space, SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_GT(test, addr, 0L);

	base = (void *)(uintptr_t)addr;

	/* interior unmap returns -EINVAL */
	ret = nommu_swmmu_unmap(ctx->space, addr +1, SWMMU_PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* partial unmap returns -EINVAL */
	ret = nommu_swmmu_unmap(ctx->space, addr, SWMMU_PAGE_SIZE/2);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* whole mapping unmaps */
	ret = nommu_swmmu_unmap(ctx->space, addr, SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* access afterward returns -EFAULT */
	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)base,
						sizeof(u64),
						0),
			-EFAULT);

	/* double unmap returns -EINVAL */
	ret = nommu_swmmu_unmap(ctx->space, addr, SWMMU_PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void nommu_swmmu_remap_shrink_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	long ret, addr;
	void *base;

	addr = nommu_swmmu_map(ctx->space, SWMMU_PAGE_SIZE * 2);
	KUNIT_ASSERT_GT(test, addr, 0L);

	base = (void *)(uintptr_t)addr;
	nommu_swmmu_store_u64(base, sizeof(u64), 671ULL);

	/* old mapping is accessible before shrink */
	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)base,
						sizeof(u64),
						0),
			0);

	/* equal size remap should be no-op */
	ret = nommu_swmmu_remap(ctx->space,
				addr,
				SWMMU_PAGE_SIZE * 2,
				SWMMU_PAGE_SIZE * 2);
	KUNIT_EXPECT_EQ(test, ret, addr);

	/* but if base addr is different, should fail */
	ret = nommu_swmmu_remap(ctx->space,
				addr + SWMMU_PAGE_SIZE,
				SWMMU_PAGE_SIZE * 2,
				SWMMU_PAGE_SIZE * 2);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* shrink should succeed */
	ret = nommu_swmmu_remap(ctx->space, addr,
				SWMMU_PAGE_SIZE * 2, SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_EQ(test, ret, addr);

	/* new range is accessible */
	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)base,
						SWMMU_PAGE_SIZE,
						0),
			0);

	/* removed tail is inaccessible */
	KUNIT_EXPECT_EQ(test,
		nommu_swmmu_check_access(ctx->space,
					(uintptr_t)base +
					SWMMU_PAGE_SIZE,
					1,
					0),
		-EFAULT);

	/* original contents are preserved */
	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_load_u64(base, sizeof(u64)),
			671ULL);

	ret = nommu_swmmu_unmap(ctx->space, addr, SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_EQ(test, ret, 0);
}

static void nommu_swmmu_remap_grow_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	long ret, addr;
	void *base;

	addr = nommu_swmmu_map(ctx->space, SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_GT(test, addr, 0L);

	base = (void *)(uintptr_t)addr;
	nommu_swmmu_store_u64(base, sizeof(u64), 771ULL);

	/* invalid old size */
	ret = nommu_swmmu_remap(ctx->space,
			addr,
			SWMMU_PAGE_SIZE * 2,
			SWMMU_PAGE_SIZE * 3);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* grow should succeed */
	ret = nommu_swmmu_remap(ctx->space, addr,
				SWMMU_PAGE_SIZE, SWMMU_PAGE_SIZE * 2);
	KUNIT_ASSERT_EQ(test, ret, addr);

	/* existing contents survive growth */
	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_load_u64(base,
					sizeof(u64)),
			771ULL);

	/* new pages are zero-filled */
	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_load_u64((void *)((uintptr_t)base + SWMMU_PAGE_SIZE),
					sizeof(u64)),
				0ULL);

	/* new range is accessible */
	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						((uintptr_t)base + SWMMU_PAGE_SIZE),
						1,
						0),
			0);

	ret = nommu_swmmu_unmap(ctx->space, addr, SWMMU_PAGE_SIZE * 2);
	KUNIT_ASSERT_EQ(test, ret, 0);
}

static void nommu_swmmu_remap_rollback_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	long ret, addr;
	void *base;

	addr = nommu_swmmu_map(ctx->space, SWMMU_PAGE_SIZE * 2);
	KUNIT_ASSERT_GT(test, addr, 0L);

	base = (void *)(uintptr_t)addr;
	nommu_swmmu_store_u64(base, sizeof(u64), 871ULL);

	/* failed growth leaves old mapping unchanged */
	/* verify rollback when allocating the grown page array fails. */
	test_alloc_fail_at(TEST_FAIL_ZALLOC, 0);
	ret = nommu_swmmu_remap(ctx->space, addr,
				SWMMU_PAGE_SIZE * 2,
				SWMMU_PAGE_SIZE * 4);
	KUNIT_EXPECT_EQ(test, ret, -ENOMEM);
	expect_allocations_balanced(test);

	/* old contents remain readable */
	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_load_u64(base,
					sizeof(u64)),
			871ULL);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)base +
						SWMMU_PAGE_SIZE,
						1,
						0),
			0);
	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)base +
						SWMMU_PAGE_SIZE * 2,
						SWMMU_PAGE_SIZE,
						0),
			-EFAULT);

	/* verify rollback when allocating the actual pages fails. */
	for (int fail_at = 0; fail_at < 2; fail_at++) {
		test_alloc_fail_at(TEST_FAIL_PAGE_ALLOC, fail_at);
		ret = nommu_swmmu_remap(ctx->space, addr,
					SWMMU_PAGE_SIZE * 2,
					SWMMU_PAGE_SIZE * 4);
		KUNIT_EXPECT_EQ(test, ret, -ENOMEM);

		expect_allocations_balanced(test);

		KUNIT_EXPECT_EQ(test,
				nommu_swmmu_check_access(ctx->space,
							(uintptr_t)base,
							SWMMU_PAGE_SIZE,
							0),
				0);
		KUNIT_EXPECT_EQ(test,
				nommu_swmmu_check_access(ctx->space,
							(uintptr_t)base +
							SWMMU_PAGE_SIZE * 2,
							1,
							0),
				-EFAULT);

		/* old contents remain readable */
		KUNIT_EXPECT_EQ(test,
				nommu_swmmu_load_u64(base,
						sizeof(u64)),
				871ULL);

		/* old length remains valid */
		KUNIT_EXPECT_EQ(test,
				nommu_swmmu_check_access(ctx->space,
							(uintptr_t)base,
							SWMMU_PAGE_SIZE,
							0),
				0);
		KUNIT_EXPECT_EQ(test,
				nommu_swmmu_check_access(ctx->space,
							(uintptr_t)base +
							SWMMU_PAGE_SIZE * 2,
							1,
							0),
				-EFAULT);

		/* new range is not visible */
		KUNIT_EXPECT_EQ(test,
				nommu_swmmu_check_access(ctx->space,
							(uintptr_t)base +
							SWMMU_PAGE_SIZE * 2 + 1,
							sizeof(u64),
							0),
				-EFAULT);
	}

	test_alloc_reset();
	ret = nommu_swmmu_unmap(ctx->space, addr, SWMMU_PAGE_SIZE * 2);
	KUNIT_ASSERT_EQ(test, ret, 0);
}

static void nommu_swmmu_remap_overlap_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	long first;
	long second;
	long ret;

	first = nommu_swmmu_map(ctx->space, SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_GT(test, first, 0L);

	second = nommu_swmmu_map(ctx->space, SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_GT(test, second, 0L);

	ret = nommu_swmmu_remap(ctx->space,
				(unsigned long)first,
				SWMMU_PAGE_SIZE,
				SWMMU_PAGE_SIZE * 2);
	KUNIT_EXPECT_EQ(test, ret, -EEXIST);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)first,
						SWMMU_PAGE_SIZE,
						0),
			0);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(ctx->space,
						(uintptr_t)second,
						SWMMU_PAGE_SIZE,
						0),
			0);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_unmap(ctx->space,
					 (unsigned long)first,
					 0),
			0);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_unmap(ctx->space,
					 (unsigned long)second,
					 0),
			0);
}

static void map_at_exact_address_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	long ret;

	/* MAP_FIXED allocation */
	ret = nommu_swmmu_map_at(ctx->space, 0x1000000000ULL + 0x1000,
				0x1000, true);
	KUNIT_ASSERT_EQ(test, ret, 0x1000001000ULL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_unmap(ctx->space,
					(unsigned long)ret,
					0),
			0);
}

static void map_at_overlap_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	long ret, mapped;

	mapped = nommu_swmmu_map_at(ctx->space, 0x1000000000ULL + 0x1000,
				0x1000, true);
	KUNIT_ASSERT_EQ(test, mapped, 0x1000001000ULL);

	/* map overlapping region should fail */
	ret = nommu_swmmu_map_at(ctx->space, 0x1000000000ULL, 0x2000, true);
	KUNIT_EXPECT_EQ(test, ret, -EEXIST);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_unmap(ctx->space,
					(unsigned long)mapped,
					0),
			0);
}

static void map_at_alignment_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	long ret;

	/* MAP_FIXED allocation, with unaligned address */
	ret = nommu_swmmu_map_at(ctx->space, 0x1000000000ULL + 0x10,
				0x1000, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void map_at_overflow_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	long ret;

	/* MAP_FIXED size is huge */
	ret = nommu_swmmu_map_at(ctx->space, 0x0, ULONG_MAX, true);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* specified address is out of range */
	ret = nommu_swmmu_map_at(ctx->space, (unsigned long)LONG_MAX + 1,
				SWMMU_PAGE_SIZE, true);
	KUNIT_EXPECT_EQ(test, ret, -EOVERFLOW);
}

static void same_address_different_spaces_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	struct nommu_swmmu_space *child;
	long ret;

	ret = swmmu_clone_space(ctx->space, &child);
	KUNIT_ASSERT_EQ(test, ret, 0L);
	KUNIT_ASSERT_NOT_NULL(test, child);

	ret = nommu_swmmu_map_at(ctx->space, 0x1000000000ULL,
				SWMMU_PAGE_SIZE, true);
	KUNIT_ASSERT_EQ(test, ret, 0x1000000000ULL);

	/* child */
	//	nommu_swmmu_kunit_set_space(child);
	ret = nommu_swmmu_map_at(child, 0x1000000000ULL,
				SWMMU_PAGE_SIZE, true);
	KUNIT_ASSERT_EQ(test, ret, 0x1000000000ULL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_unmap(ctx->space,
					(unsigned long)ret,
					0),
			0);
	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_unmap(child,
					(unsigned long)ret,
					0),
			0);
}

static void nommu_swmmu_mode_test(struct kunit *test)
{
	struct nommu_swmmu_space *space;
	struct nommu_swmmu_space *old_space;
	struct mm_struct *test_mm;
	long ret;

	space = nommu_swmmu_space_create_with_ops(&test_ops);
	KUNIT_ASSERT_NOT_NULL(test, space);

	test_mm = mm_alloc();
	KUNIT_ASSERT_NOT_NULL(test, test_mm);

	old_space = test_mm->swmmu_space;
	KUNIT_ASSERT_NOT_NULL(test, old_space);

	/*
	 * mm_alloc() creates and attaches an initial SWMMU space.
	 * Remove that mm-owned reference before testing attach().
	 */
	nommu_swmmu_space_detach(test_mm);

	/*
	 * Test the attach helper directly. Do not rely on
	 * nommu_swmmu_space_create_with_ops(&test_ops) implicitly using current->mm.
	 */
	ret = nommu_swmmu_space_attach(test_mm, space);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_PTR_EQ(test, test_mm->swmmu_space, space);

	/* default mode is OFF */
	KUNIT_EXPECT_EQ(test, nommu_swmmu_get_mode(test_mm), NOMMU_SWMMU_OFF);

	/* set ON succeeds */
	KUNIT_EXPECT_EQ(test, nommu_swmmu_set_mode(test_mm, NOMMU_SWMMU_ON),
			0);

	/* GET returns ON */
	KUNIT_EXPECT_EQ(test, nommu_swmmu_get_mode(test_mm), NOMMU_SWMMU_ON);

	/* invalid mode returns -EINVAL */
	KUNIT_EXPECT_EQ(test, nommu_swmmu_set_mode(test_mm, 100), -EINVAL);

	/* set OFF succeeds when no mappings exist */
	KUNIT_EXPECT_EQ(test, nommu_swmmu_set_mode(test_mm, NOMMU_SWMMU_OFF),
			0);

	/* set ON succeeds */
	KUNIT_EXPECT_EQ(test, nommu_swmmu_set_mode(test_mm, NOMMU_SWMMU_ON),
			0);

	ret = nommu_swmmu_map_at(space, 0x1000000000ULL,
				SWMMU_PAGE_SIZE, true);
	KUNIT_ASSERT_EQ(test, ret, 0x1000000000ULL);

	/* set OFF returns -EBUSY while a SWMMU mapping exists */
	KUNIT_EXPECT_EQ(test, nommu_swmmu_set_mode(test_mm, NOMMU_SWMMU_OFF),
			-EBUSY);

	/* mode changes do not affect existing mappings */
	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_check_access(space,
						(uintptr_t)0x1000000000ULL,
						SWMMU_PAGE_SIZE,
						0),
			0);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_unmap(space,
					0x1000000000UL,
					0),
			0);

	nommu_swmmu_space_detach(test_mm);
	mmput(test_mm);
}


/* init/exit */
static int nommu_swmmu_test_init(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx;

	test_alloc_reset();

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->space = nommu_swmmu_space_create_with_ops(&test_ops);
	KUNIT_ASSERT_NOT_NULL(test, ctx->space);

	nommu_swmmu_kunit_set_space(ctx->space);
	test->priv = ctx;

	return 0;
}

static void nommu_swmmu_test_exit(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;

	test_alloc_reset();

	nommu_swmmu_kunit_clear_space();

	if (ctx && ctx->space)
		nommu_swmmu_space_put(ctx->space);
}


static struct kunit_case nommu_swmmu_test_cases[] = {
	KUNIT_CASE(nommu_swmmu_increment_test),
	KUNIT_CASE(nommu_swmmu_clone_test),
	KUNIT_CASE(nommu_swmmu_attach_test),
	KUNIT_CASE(nommu_swmmu_cross_page_test),
	KUNIT_CASE(nommu_swmmu_translate_boundary_test),
	KUNIT_CASE(nommu_swmmu_multiple_mapping_test),
	KUNIT_CASE(nommu_swmmu_free_test),
	KUNIT_CASE(nommu_swmmu_free_interior_test),
	KUNIT_CASE(nommu_swmmu_repeated_alloc_free_test),
	KUNIT_CASE(nommu_swmmu_repeated_clone_test),
	KUNIT_CASE(nommu_swmmu_alloc_failure_test),
	KUNIT_CASE(nommu_swmmu_alloc_page_failure_test),
	KUNIT_CASE(nommu_swmmu_clone_failure_test),
	KUNIT_CASE(nommu_swmmu_clone_page_failure_test),
	KUNIT_CASE(nommu_swmmu_clone_copy_failure_test),
	KUNIT_CASE(nommu_swmmu_map_test),
	KUNIT_CASE(nommu_swmmu_unmap_test),
	KUNIT_CASE(nommu_swmmu_remap_shrink_test),
	KUNIT_CASE(nommu_swmmu_remap_grow_test),
	KUNIT_CASE(nommu_swmmu_remap_rollback_test),
	KUNIT_CASE(nommu_swmmu_remap_overlap_test),
	KUNIT_CASE(map_at_exact_address_test),
	KUNIT_CASE(map_at_overlap_test),
	KUNIT_CASE(map_at_alignment_test),
	KUNIT_CASE(map_at_overflow_test),
	KUNIT_CASE(same_address_different_spaces_test),
	KUNIT_CASE(nommu_swmmu_mode_test),
	{}
};

static struct kunit_suite nommu_swmmu_test_suite = {
	.name = "nommu-swmmu",
	.init = nommu_swmmu_test_init,
	.exit = nommu_swmmu_test_exit,
	.test_cases = nommu_swmmu_test_cases,
};

kunit_test_suite(nommu_swmmu_test_suite);
MODULE_LICENSE("GPL");
