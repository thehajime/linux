// mm/tests/nommu_swmmu_kunit.c

#include <kunit/test.h>
#include <linux/highmem.h>
#include <linux/nommu_swmmu.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/mman.h>

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

	return test_alloc_state.count++ == test_alloc_state.fail_at;
}

static void *test_kzalloc(size_t size, gfp_t gfp)
{
	void *ptr;

	if (test_should_fail(TEST_FAIL_ZALLOC))
		return NULL;

	ptr = kzalloc(size, gfp);
	if (ptr)
		test_alloc_state.zalloc_success++;

	return ptr;
}

static void test_kfree(void *ptr)
{
	if (ptr)
		test_alloc_state.dealloc_count++;

	kfree(ptr);
}

static struct page *test_alloc_page(gfp_t gfp)
{
	struct page *page;

	if (test_should_fail(TEST_FAIL_PAGE_ALLOC))
		return NULL;

	page = alloc_page(gfp);
	if (page)
		test_alloc_state.page_alloc_success++;

	return page;
}

static void test_free_page(struct page *page)
{
	if (page)
		test_alloc_state.page_free_count++;

	__free_page(page);
}

static int test_copy_page(struct page *dst, const struct page *src)
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

struct nommu_swmmu_test_ctx {
	struct nommu_swmmu_space *space;
};

struct nommu_swmmu_mm_test_ctx {
	struct mm_struct *mm;
	struct nommu_swmmu_space *space;
};

static void expect_allocations_balanced(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test,
			test_alloc_state.zalloc_success,
			test_alloc_state.dealloc_count);

	KUNIT_EXPECT_EQ(test,
			test_alloc_state.page_alloc_success,
			test_alloc_state.page_free_count);
}

static int nommu_swmmu_test_init(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	ctx->space = nommu_swmmu_space_create_with_ops(&test_ops);
	KUNIT_ASSERT_NOT_NULL(test, ctx->space);

	test->priv = ctx;
	return 0;
}

static void nommu_swmmu_test_exit(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;

	if (ctx && ctx->space)
		nommu_swmmu_space_put(ctx->space);

	test_alloc_reset();
}

static void nommu_swmmu_backing_alloc_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	struct nommu_swmmu_backing *backing = NULL;
	size_t i;
	int ret;

	ret = nommu_swmmu_kunit_backing_alloc(ctx->space,
					      2 * SWMMU_PAGE_SIZE,
					      &backing);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_NOT_NULL(test, backing);
	KUNIT_EXPECT_EQ(test, backing->page_count, 2UL);

	for (i = 0; i < backing->page_count; i++) {
		void *addr = page_address(backing->pages[i]);

		KUNIT_ASSERT_NOT_NULL(test, addr);
		KUNIT_EXPECT_EQ(test, ((u8 *)addr)[0], 0);
		KUNIT_EXPECT_EQ(test, ((u8 *)addr)[SWMMU_PAGE_SIZE - 1], 0);
	}

	nommu_swmmu_kunit_backing_release(ctx->space, backing);
}

static void nommu_swmmu_backing_zalloc_failure_test(
	struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	struct nommu_swmmu_backing *backing;
	unsigned int fail_at;
	int ret;

	for (fail_at = 0; fail_at < 2; fail_at++) {
		test_alloc_fail_at(TEST_FAIL_ZALLOC, fail_at);
		backing = NULL;

		ret = nommu_swmmu_kunit_backing_alloc(
			ctx->space,
			SWMMU_PAGE_SIZE,
			&backing);

		KUNIT_EXPECT_LT(test, ret, 0);
		KUNIT_EXPECT_PTR_EQ(test, backing, NULL);
		expect_allocations_balanced(test);

		test_alloc_reset();

		ret = nommu_swmmu_kunit_backing_alloc(
			ctx->space,
			SWMMU_PAGE_SIZE,
			&backing);

		KUNIT_ASSERT_EQ(test, ret, 0);
		KUNIT_ASSERT_NOT_NULL(test, backing);

		nommu_swmmu_kunit_backing_release(ctx->space, backing);
		expect_allocations_balanced(test);
	}
}

static void nommu_swmmu_backing_page_failure_test(
	struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	struct nommu_swmmu_backing *backing;
	unsigned int fail_at;
	int ret;

	for (fail_at = 0; fail_at < 4; fail_at++) {
		test_alloc_fail_at(TEST_FAIL_PAGE_ALLOC, fail_at);
		backing = NULL;

		ret = nommu_swmmu_kunit_backing_alloc(
			ctx->space,
			4 * SWMMU_PAGE_SIZE,
			&backing);

		KUNIT_EXPECT_LT(test, ret, 0);
		KUNIT_EXPECT_PTR_EQ(test, backing, NULL);
		expect_allocations_balanced(test);

		test_alloc_reset();

		ret = nommu_swmmu_kunit_backing_alloc(
			ctx->space,
			4 * SWMMU_PAGE_SIZE,
			&backing);

		KUNIT_ASSERT_EQ(test, ret, 0);
		KUNIT_ASSERT_NOT_NULL(test, backing);

		nommu_swmmu_kunit_backing_release(ctx->space, backing);
		expect_allocations_balanced(test);
	}
}

static void nommu_swmmu_vma_dup_offset_test(struct kunit *test)
{
	struct nommu_swmmu_test_ctx *ctx = test->priv;
	struct nommu_swmmu_backing *backing = NULL;
	struct nommu_swmmu_vma src = {};
	struct nommu_swmmu_vma *dst = NULL;
	u32 *page;
	int ret;

	ret = nommu_swmmu_kunit_backing_alloc(
		ctx->space,
		3 * SWMMU_PAGE_SIZE,
		&backing);
	KUNIT_ASSERT_EQ(test, ret, 0);

	page = page_address(backing->pages[1]);
	KUNIT_ASSERT_NOT_NULL(test, page);
	*page = 0x11223344;

	page = page_address(backing->pages[2]);
	KUNIT_ASSERT_NOT_NULL(test, page);
	*page = 0x55667788;

	src.backing = backing;
	src.page_offset = 1;
	src.page_count = 2;
	src.access = NOMMU_SWMMU_READ | NOMMU_SWMMU_WRITE;

	ret = nommu_swmmu_kunit_vma_dup(ctx->space, &src, &dst);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_NOT_NULL(test, dst);

	KUNIT_EXPECT_EQ(test, dst->page_offset, 0UL);
	KUNIT_EXPECT_EQ(test, dst->page_count, 2UL);
	KUNIT_EXPECT_EQ(test, dst->access, src.access);
	KUNIT_EXPECT_PTR_NE(test, dst->backing, src.backing);

	page = page_address(dst->backing->pages[0]);
	KUNIT_ASSERT_NOT_NULL(test, page);
	KUNIT_EXPECT_EQ(test, *page, 0x11223344);

	page = page_address(dst->backing->pages[1]);
	KUNIT_ASSERT_NOT_NULL(test, page);
	KUNIT_EXPECT_EQ(test, *page, 0x55667788);

	nommu_swmmu_kunit_vma_release(ctx->space, dst);
	nommu_swmmu_kunit_backing_release(ctx->space, backing);
}

static struct kunit_case nommu_swmmu_test_cases[] = {
	KUNIT_CASE(nommu_swmmu_backing_alloc_test),
	KUNIT_CASE(nommu_swmmu_backing_zalloc_failure_test),
	KUNIT_CASE(nommu_swmmu_backing_page_failure_test),
	KUNIT_CASE(nommu_swmmu_vma_dup_offset_test),
	{}
};


/* explicit mm-backed tests */
static void nommu_swmmu_mm_mapping_test(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *ctx = test->priv;
	unsigned long address;
	u64 value;
	int ret;

	address = nommu_swmmu_kunit_mmap_mm(
		ctx->mm,
		0,
		SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS);
	KUNIT_ASSERT_GT(test, address, 0UL);

	ret = nommu_swmmu_kunit_store_mm(ctx->mm,
					 address,
					 sizeof(value),
					 0x12345678);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = nommu_swmmu_kunit_load_mm(ctx->mm,
					address,
					sizeof(value),
					&value);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, value, 0x12345678ULL);

	ret = nommu_swmmu_kunit_unmap_mm(ctx->mm,
					 address,
					 SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = nommu_swmmu_kunit_load_mm(ctx->mm,
					address,
					sizeof(value),
					&value);
	KUNIT_EXPECT_EQ(test, ret, -EFAULT);
}

static void nommu_swmmu_mm_mremap_shrink_test(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *ctx = test->priv;
	unsigned long address;
	unsigned long ret;
	u64 value;

	address = nommu_swmmu_kunit_mmap_mm(
		ctx->mm,
		0,
		SWMMU_PAGE_SIZE * 2,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS);
	KUNIT_ASSERT_GT(test, address, 0UL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(ctx->mm,
						   address,
						   sizeof(value),
						   0x12345678),
			0);

	ret = nommu_swmmu_kunit_mremap_mm(
		ctx->mm,
		address,
		SWMMU_PAGE_SIZE * 2,
		SWMMU_PAGE_SIZE,
		0,
		0);
	KUNIT_ASSERT_EQ(test, ret, address);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_kunit_load_mm(ctx->mm,
						  address,
						  sizeof(value),
						  &value),
			0);
	KUNIT_EXPECT_EQ(test, value, 0x12345678ULL);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_kunit_load_mm(
				ctx->mm,
				address + SWMMU_PAGE_SIZE,
				sizeof(value),
				&value),
			-EFAULT);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_unmap_mm(ctx->mm,
						   address,
						   SWMMU_PAGE_SIZE),
			0);
}

static void nommu_swmmu_mm_mremap_expand_test(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *ctx = test->priv;
	unsigned long address;
	unsigned long ret;
	u64 value;

	address = nommu_swmmu_kunit_mmap_mm(
		ctx->mm, 0, SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS);
	KUNIT_ASSERT_GT(test, address, 0UL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(ctx->mm,
						   address,
						   sizeof(value),
						   0x1122334455667788ULL),
			0);

	ret = nommu_swmmu_kunit_mremap_mm(
		ctx->mm,
		address,
		SWMMU_PAGE_SIZE,
		SWMMU_PAGE_SIZE * 2,
		0,
		0);
	KUNIT_ASSERT_EQ(test, ret, address);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_load_mm(ctx->mm,
						  address,
						  sizeof(value),
						  &value),
			0);
	KUNIT_EXPECT_EQ(test, value, 0x1122334455667788ULL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_load_mm(
				ctx->mm,
				address + SWMMU_PAGE_SIZE,
				sizeof(value),
				&value),
			0);
	KUNIT_EXPECT_EQ(test, value, 0ULL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_unmap_mm(ctx->mm,
						   address,
						   SWMMU_PAGE_SIZE * 2),
			0);
}

static void nommu_swmmu_mm_mremap_expand_rollback_test(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *ctx = test->priv;
	unsigned long address;
	unsigned long ret;
	u64 value;

	address = nommu_swmmu_kunit_mmap_mm(
		ctx->mm,
		0,
		SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS);
	KUNIT_ASSERT_GT(test, address, 0UL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(ctx->mm,
						   address,
						   sizeof(value),
						   0x12345678),
			0);

	/*
	 * Fail while preparing the new expanded backing.
	 */
	test_alloc_fail_at(TEST_FAIL_ZALLOC, 0);

	ret = nommu_swmmu_kunit_mremap_mm(
		ctx->mm,
		address,
		SWMMU_PAGE_SIZE,
		SWMMU_PAGE_SIZE * 2,
		0,
		0);

	KUNIT_EXPECT_EQ(test, ret, (unsigned long)-ENOMEM);

	test_alloc_reset();

	/*
	 * Original VMA/backing remains intact.
	 */
	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_load_mm(ctx->mm,
						  address,
						  sizeof(value),
						  &value),
			0);
	KUNIT_EXPECT_EQ(test, value, 0x12345678ULL);

	KUNIT_EXPECT_EQ(test,
			nommu_swmmu_kunit_load_mm(
				ctx->mm,
				address + SWMMU_PAGE_SIZE,
				sizeof(value),
				&value),
			-EFAULT);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_unmap_mm(ctx->mm,
						   address,
						   SWMMU_PAGE_SIZE),
			0);
}

static int nommu_swmmu_mm_ctx_create(struct nommu_swmmu_mm_test_ctx *ctx,
				struct kunit *test)
{
	struct nommu_swmmu_space *old_space;
	int ret;

	if (!ctx)
		return -EINVAL;

	ctx->mm = mm_alloc();
	if (!ctx->mm)
		return -ENOMEM;

	old_space = ctx->mm->swmmu_space;
	if (!old_space) {
		mmput(ctx->mm);
		ctx->mm = NULL;
		return -EINVAL;
	}

	nommu_swmmu_space_detach(ctx->mm);

	ctx->space = nommu_swmmu_space_create_with_ops(&test_ops);
	if (!ctx->space) {
		mmput(ctx->mm);
		ctx->mm = NULL;
		return -ENOMEM;
	}

	ret = nommu_swmmu_space_attach(ctx->mm, ctx->space);
	if (ret) {
		nommu_swmmu_space_put(ctx->space);
		mmput(ctx->mm);
		ctx->space = NULL;
		ctx->mm = NULL;
		return ret;
	}

	ctx->mm->swmmu_mode = NOMMU_SWMMU_ON;

	return 0;
}

static int test_dup_mmap(struct mm_struct *dst,
			 struct mm_struct *src)
{
	int ret;

	mmap_write_lock(src);
	mmap_write_lock_nested(dst, SINGLE_DEPTH_NESTING);

	ret = nommu_swmmu_dup_mmap(dst, src);

	mmap_write_unlock(dst);
	mmap_write_unlock(src);

	return ret;
}

static void nommu_swmmu_mm_ctx_destroy(
	struct nommu_swmmu_mm_test_ctx *ctx)
{
	if (!ctx || !ctx->mm)
		return;

	nommu_swmmu_space_detach(ctx->mm);
	mmput(ctx->mm);

	ctx->mm = NULL;
	ctx->space = NULL;
}

KUNIT_DEFINE_ACTION_WRAPPER(
	nommu_swmmu_mm_ctx_destroy_action,
	nommu_swmmu_mm_ctx_destroy,
	struct nommu_swmmu_mm_test_ctx *);

static void nommu_swmmu_mm_fork_clone_test(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *parent;
	struct nommu_swmmu_mm_test_ctx *child;
	unsigned long address;
	u64 value;
	int ret;

	parent = kunit_kzalloc(test, sizeof(*parent), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, parent);

	child = kunit_kzalloc(test, sizeof(*child), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, child);

	ret = nommu_swmmu_mm_ctx_create(parent, test);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_add_action_or_reset(test,
					nommu_swmmu_mm_ctx_destroy_action,
					parent);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = nommu_swmmu_mm_ctx_create(child, test);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_add_action_or_reset(test,
					nommu_swmmu_mm_ctx_destroy_action,
					child);
	KUNIT_ASSERT_EQ(test, ret, 0);



	address = nommu_swmmu_kunit_mmap_mm(
		parent->mm,
		0,
		SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS);
	KUNIT_ASSERT_GT(test, address, 0UL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(parent->mm,
						   address,
						   sizeof(value),
						   41),
			0);

	ret = test_dup_mmap(child->mm, parent->mm);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_load_mm(child->mm,
						  address,
						  sizeof(value),
						  &value),
			0);
	KUNIT_EXPECT_EQ(test, value, 41ULL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(child->mm,
						   address,
						   sizeof(value),
						   42),
			0);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_load_mm(parent->mm,
						  address,
						  sizeof(value),
						  &value),
			0);
	KUNIT_EXPECT_EQ(test, value, 41ULL);
}

static void nommu_swmmu_mm_fork_rollback_test(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *parent;
	struct nommu_swmmu_mm_test_ctx *child;
	unsigned long address;
	u64 value;
	int ret;

	parent = kunit_kzalloc(test, sizeof(*parent), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, parent);

	child = kunit_kzalloc(test, sizeof(*child), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, child);

	ret = nommu_swmmu_mm_ctx_create(parent, test);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_add_action_or_reset(test,
					nommu_swmmu_mm_ctx_destroy_action,
					parent);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = nommu_swmmu_mm_ctx_create(child, test);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_add_action_or_reset(test,
					nommu_swmmu_mm_ctx_destroy_action,
					child);
	KUNIT_ASSERT_EQ(test, ret, 0);


	address = nommu_swmmu_kunit_mmap_mm(
		parent->mm,
		0,
		SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS);
	KUNIT_ASSERT_GT(test, address, 0UL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(parent->mm,
						   address,
						   sizeof(value),
						   41),
			0);

	test_alloc_fail_at(TEST_FAIL_ZALLOC, 0);

	ret = test_dup_mmap(child->mm, parent->mm);
	KUNIT_EXPECT_EQ(test, ret, -ENOMEM);

	test_alloc_reset();

	/*
	 * Parent remains intact.
	 */
	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_load_mm(parent->mm,
						  address,
						  sizeof(value),
						  &value),
			0);
	KUNIT_EXPECT_EQ(test, value, 41ULL);

	/*
	 * Child must not retain a partially published VMA.
	 */
	KUNIT_EXPECT_EQ(test, child->mm->map_count, 0);
}

#define TEST_VMA1_ADDR	0x1000000000UL
#define TEST_VMA2_ADDR	0x1000002000UL

static void nommu_swmmu_mm_fork_page_copy_rollback_test(
	struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *parent;
	struct nommu_swmmu_mm_test_ctx *child;
	unsigned long addr1;
	unsigned long addr2;
	u64 value;
	unsigned int fail_at;
	int ret;

	parent = kunit_kzalloc(test, sizeof(*parent), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, parent);

	child = kunit_kzalloc(test, sizeof(*child), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, child);

	ret = nommu_swmmu_mm_ctx_create(parent, test);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = nommu_swmmu_mm_ctx_create(child, test);
	KUNIT_ASSERT_EQ(test, ret, 0);

	addr1 = nommu_swmmu_kunit_mmap_mm(
		parent->mm,
		TEST_VMA1_ADDR,
		SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS |
		MAP_FIXED_NOREPLACE);
	KUNIT_ASSERT_EQ(test, addr1, TEST_VMA1_ADDR);

	addr2 = nommu_swmmu_kunit_mmap_mm(
		parent->mm,
		TEST_VMA2_ADDR,
		SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS |
		MAP_FIXED_NOREPLACE);
	KUNIT_ASSERT_EQ(test, addr2, TEST_VMA2_ADDR);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(parent->mm,
						   addr1,
						   sizeof(value),
						   41),
			0);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(parent->mm,
						   addr2,
						   sizeof(value),
						   42),
			0);

	/*
	 * The exact upper bound should match the number of page-copy
	 * operations performed by this clone.
	 */
	for (fail_at = 0; fail_at < 2; fail_at++) {
		test_alloc_reset();
		test_alloc_fail_at(TEST_FAIL_PAGE_COPY, fail_at);

		ret = test_dup_mmap(child->mm, parent->mm);
		KUNIT_EXPECT_EQ(test, ret, -ENOMEM);

		test_alloc_reset();

		/*
		 * No child VMA or child SWMMU metadata may remain.
		 */
		KUNIT_EXPECT_EQ(test, child->mm->map_count, 0);

		KUNIT_EXPECT_EQ(test,
				nommu_swmmu_kunit_load_mm(parent->mm,
							  addr1,
							  sizeof(value),
							  &value),
				0);
		KUNIT_EXPECT_EQ(test, value, 41ULL);

		KUNIT_EXPECT_EQ(test,
				nommu_swmmu_kunit_load_mm(parent->mm,
							  addr2,
							  sizeof(value),
							  &value),
				0);
		KUNIT_EXPECT_EQ(test, value, 42ULL);

		expect_allocations_balanced(test);
	}

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_unmap_mm(parent->mm,
						   addr1,
						   SWMMU_PAGE_SIZE),
			0);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_unmap_mm(parent->mm,
						   addr2,
						   SWMMU_PAGE_SIZE),
			0);
}

/* pdate when the fork clone allocation sequence changes */
#define SWMMU_FORK_ZALLOC_POINTS	8
#define SWMMU_FORK_PAGE_ALLOC_POINTS	2

static void nommu_swmmu_mm_fork_zalloc_rollback_test(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *parent;
	struct nommu_swmmu_mm_test_ctx *child;
	unsigned long addr1;
	unsigned long addr2;
	int ret;
	unsigned int fail_at;

	parent = kunit_kzalloc(test, sizeof(*parent), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, parent);

	child = kunit_kzalloc(test, sizeof(*child), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, child);

	ret = nommu_swmmu_mm_ctx_create(parent, test);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = nommu_swmmu_mm_ctx_create(child, test);
	KUNIT_ASSERT_EQ(test, ret, 0);


	addr1 = nommu_swmmu_kunit_mmap_mm(
		parent->mm, 0x1000000000UL, SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE);
	KUNIT_ASSERT_EQ(test, addr1, 0x1000000000UL);

	addr2 = nommu_swmmu_kunit_mmap_mm(
		parent->mm, 0x1000002000UL, SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE);
	KUNIT_ASSERT_EQ(test, addr2, 0x1000002000UL);

	/*
	 * Update this bound if the clone allocation layout changes.
	 */
	for (fail_at = 0; fail_at < SWMMU_FORK_ZALLOC_POINTS;
	     fail_at++) {
		test_alloc_reset();
		test_alloc_fail_at(TEST_FAIL_ZALLOC, fail_at);

		ret = test_dup_mmap(child->mm, parent->mm);
		KUNIT_EXPECT_EQ(test, ret, -ENOMEM);

		test_alloc_reset();

		KUNIT_EXPECT_EQ(test, child->mm->map_count, 0);
		expect_allocations_balanced(test);
	}
}

static void nommu_swmmu_mm_fork_page_alloc_rollback_test(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *parent;
	struct nommu_swmmu_mm_test_ctx *child;
	unsigned long addr1;
	unsigned long addr2;
	int ret;
	unsigned int fail_at;

	parent = kunit_kzalloc(test, sizeof(*parent), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, parent);

	child = kunit_kzalloc(test, sizeof(*child), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, child);

	ret = nommu_swmmu_mm_ctx_create(parent, test);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = nommu_swmmu_mm_ctx_create(child, test);
	KUNIT_ASSERT_EQ(test, ret, 0);

	addr1 = nommu_swmmu_kunit_mmap_mm(
		parent->mm, 0x1000000000UL, SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE);
	KUNIT_ASSERT_EQ(test, addr1, 0x1000000000UL);

	addr2 = nommu_swmmu_kunit_mmap_mm(
		parent->mm, 0x1000002000UL, SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE);
	KUNIT_ASSERT_EQ(test, addr2, 0x1000002000UL);


	for (fail_at = 0;
	     fail_at < SWMMU_FORK_PAGE_ALLOC_POINTS;
	     fail_at++) {
		test_alloc_reset();
		test_alloc_fail_at(TEST_FAIL_PAGE_ALLOC, fail_at);

		ret = test_dup_mmap(child->mm, parent->mm);
		KUNIT_EXPECT_EQ(test, ret, -ENOMEM);

		test_alloc_reset();

		KUNIT_EXPECT_EQ(test, child->mm->map_count, 0);
		expect_allocations_balanced(test);
	}
}

static void nommu_swmmu_mm_map_fixed_replace_test(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *ctx = test->priv;
	unsigned long address;
	unsigned long replacement;
	u64 value;
	int ret;

	address = nommu_swmmu_kunit_mmap_mm(
		ctx->mm,
		0x1000000000UL,
		SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS |
		MAP_FIXED_NOREPLACE);
	KUNIT_ASSERT_EQ(test, address, 0x1000000000UL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(ctx->mm,
						   address,
						   sizeof(value),
						   41),
			0);

	replacement = nommu_swmmu_kunit_mmap_mm(
		ctx->mm,
		address,
		SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS |
		MAP_FIXED);
	KUNIT_ASSERT_EQ(test, replacement, address);

	/*
	 * Replacement has independent, freshly zeroed backing.
	 */
	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_load_mm(ctx->mm,
						  address,
						  sizeof(value),
						  &value),
			0);
	KUNIT_EXPECT_EQ(test, value, 0ULL);

	/* MAP_FIXED_NOREPLACE should reject replacement */
	replacement = nommu_swmmu_kunit_mmap_mm(
		ctx->mm,
		address,
		SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS |
		MAP_FIXED_NOREPLACE);
	KUNIT_EXPECT_EQ(test,
			replacement,
			(unsigned long)-EEXIST);

	ret = nommu_swmmu_kunit_unmap_mm(ctx->mm,
					 address,
					 SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_EQ(test, ret, 0);
}

static void nommu_swmmu_mm_map_fixed_head_replace_test(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *ctx = test->priv;
	unsigned long old_addr;
	unsigned long replacement;
	u64 value;

	old_addr = nommu_swmmu_kunit_mmap_mm(
		ctx->mm,
		0x1000000000UL,
		SWMMU_PAGE_SIZE * 2,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS |
		MAP_FIXED_NOREPLACE);
	KUNIT_ASSERT_EQ(test, old_addr, 0x1000000000UL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(ctx->mm,
						   old_addr,
						   sizeof(value),
						   41),
			0);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(
				ctx->mm,
				old_addr + SWMMU_PAGE_SIZE,
				sizeof(value),
				42),
			0);

	/*
	 * Replace the first page. The second page must remain mapped
	 * by the retained suffix VMA.
	 */
	replacement = nommu_swmmu_kunit_mmap_mm(
		ctx->mm,
		old_addr,
		SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED);
	KUNIT_ASSERT_EQ(test, replacement, old_addr);

	/* Replacement backing is new and zero-filled. */
	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_load_mm(ctx->mm,
						  old_addr,
						  sizeof(value),
						  &value),
			0);
	KUNIT_EXPECT_EQ(test, value, 0ULL);

	/* Retained suffix still contains the original value. */
	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_load_mm(
				ctx->mm,
				old_addr + SWMMU_PAGE_SIZE,
				sizeof(value),
				&value),
			0);
	KUNIT_EXPECT_EQ(test, value, 42ULL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_unmap_mm(ctx->mm,
						   old_addr,
						   SWMMU_PAGE_SIZE),
			0);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_unmap_mm(
				ctx->mm,
				old_addr + SWMMU_PAGE_SIZE,
				SWMMU_PAGE_SIZE),
			0);
}

#define SWMMU_FIXED_HEAD_ZALLOC_POINTS	4
#define SWMMU_FIXED_HEAD_PAGE_POINTS	1
static void nommu_swmmu_mm_map_fixed_head_replace_rollback_test(
	struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *ctx = test->priv;
	unsigned long old_addr;
	unsigned long replacement;
	u64 value;
	unsigned int fail_at;
	int ret;

	old_addr = nommu_swmmu_kunit_mmap_mm(
		ctx->mm,
		0x1000000000UL,
		SWMMU_PAGE_SIZE * 2,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS |
		MAP_FIXED_NOREPLACE);
	KUNIT_ASSERT_EQ(test, old_addr, 0x1000000000UL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(ctx->mm,
						   old_addr,
						   sizeof(value),
						   41),
			0);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(
				ctx->mm,
				old_addr + SWMMU_PAGE_SIZE,
				sizeof(value),
				42),
			0);

	for (fail_at = 0;
	     fail_at < SWMMU_FIXED_HEAD_ZALLOC_POINTS;
	     fail_at++) {
		test_alloc_reset();
		test_alloc_fail_at(TEST_FAIL_ZALLOC, fail_at);

		replacement = nommu_swmmu_kunit_mmap_mm(
			ctx->mm,
			old_addr,
			SWMMU_PAGE_SIZE,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS |
			MAP_FIXED);

		KUNIT_EXPECT_EQ(test,
				replacement,
				(unsigned long)-ENOMEM);

		test_alloc_reset();

		/*
		 * The original two-page VMA must remain unchanged.
		 */
		KUNIT_ASSERT_EQ(test,
				nommu_swmmu_kunit_load_mm(
					ctx->mm,
					old_addr,
					sizeof(value),
					&value),
				0);
		KUNIT_EXPECT_EQ(test, value, 41ULL);

		KUNIT_ASSERT_EQ(test,
				nommu_swmmu_kunit_load_mm(
					ctx->mm,
					old_addr + SWMMU_PAGE_SIZE,
					sizeof(value),
					&value),
				0);
		KUNIT_EXPECT_EQ(test, value, 42ULL);

		expect_allocations_balanced(test);
	}

	for (fail_at = 0; fail_at < SWMMU_FIXED_HEAD_PAGE_POINTS;
	     fail_at++) {
		test_alloc_reset();
		test_alloc_fail_at(TEST_FAIL_PAGE_ALLOC, fail_at);

		replacement = nommu_swmmu_kunit_mmap_mm(
			ctx->mm,
			old_addr,
			SWMMU_PAGE_SIZE,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS |
			MAP_FIXED);

		KUNIT_EXPECT_EQ(test, replacement,
				(unsigned long)-ENOMEM);

		test_alloc_reset();

		KUNIT_ASSERT_EQ(test,
				nommu_swmmu_kunit_load_mm(
					ctx->mm,
					old_addr,
					sizeof(value),
					&value),
				0);
		KUNIT_EXPECT_EQ(test, value, 41ULL);

		KUNIT_ASSERT_EQ(test,
				nommu_swmmu_kunit_load_mm(
					ctx->mm,
					old_addr + SWMMU_PAGE_SIZE,
					sizeof(value),
					&value),
				0);
		KUNIT_EXPECT_EQ(test, value, 42ULL);

		expect_allocations_balanced(test);
	}

	ret = nommu_swmmu_kunit_unmap_mm(ctx->mm,
					 old_addr,
					 SWMMU_PAGE_SIZE * 2);
	KUNIT_ASSERT_EQ(test, ret, 0);
}

static void nommu_swmmu_mm_map_fixed_tail_replace_test(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *ctx = test->priv;
	unsigned long old_addr;
	unsigned long replacement;
	u64 value;
	int ret;

	old_addr = nommu_swmmu_kunit_mmap_mm(
		ctx->mm,
		0x1000000000UL,
		SWMMU_PAGE_SIZE * 2,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS |
		MAP_FIXED_NOREPLACE);
	KUNIT_ASSERT_EQ(test, old_addr, 0x1000000000UL);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(
				ctx->mm,
				old_addr,
				sizeof(value),
				41),
			0);

	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_store_mm(
				ctx->mm,
				old_addr + SWMMU_PAGE_SIZE,
				sizeof(value),
				42),
			0);

	replacement = nommu_swmmu_kunit_mmap_mm(
		ctx->mm,
		old_addr + SWMMU_PAGE_SIZE,
		SWMMU_PAGE_SIZE,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS |
		MAP_FIXED);
	KUNIT_ASSERT_EQ(test,
			replacement,
			old_addr + SWMMU_PAGE_SIZE);

	/* Retained prefix keeps its original data. */
	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_load_mm(
				ctx->mm,
				old_addr,
				sizeof(value),
				&value),
			0);
	KUNIT_EXPECT_EQ(test, value, 41ULL);

	/* Replacement suffix receives fresh zeroed backing. */
	KUNIT_ASSERT_EQ(test,
			nommu_swmmu_kunit_load_mm(
				ctx->mm,
				old_addr + SWMMU_PAGE_SIZE,
				sizeof(value),
				&value),
			0);
	KUNIT_EXPECT_EQ(test, value, 0ULL);

	ret = nommu_swmmu_kunit_unmap_mm(ctx->mm,
					 old_addr,
					 SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = nommu_swmmu_kunit_unmap_mm(
		ctx->mm,
		old_addr + SWMMU_PAGE_SIZE,
		SWMMU_PAGE_SIZE);
	KUNIT_ASSERT_EQ(test, ret, 0);
}

static int nommu_swmmu_mm_ctx_init(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *ctx;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = nommu_swmmu_mm_ctx_create(ctx, test);
	if (ret)
		return ret;

	test->priv = ctx;
	return 0;
}

static void nommu_swmmu_mm_ctx_exit(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *ctx = test->priv;

	if (!ctx || !ctx->mm)
		return;

	nommu_swmmu_space_detach(ctx->mm);
	mmput(ctx->mm);

	ctx->mm = NULL;
	ctx->space = NULL;
}

static struct kunit_case nommu_swmmu_mm_test_cases[] = {
	KUNIT_CASE(nommu_swmmu_mm_mapping_test),
	KUNIT_CASE(nommu_swmmu_mm_mremap_shrink_test),
	KUNIT_CASE(nommu_swmmu_mm_mremap_expand_test),
	KUNIT_CASE(nommu_swmmu_mm_mremap_expand_rollback_test),
	KUNIT_CASE(nommu_swmmu_mm_fork_clone_test),
	KUNIT_CASE(nommu_swmmu_mm_fork_rollback_test),
	KUNIT_CASE(nommu_swmmu_mm_fork_page_copy_rollback_test),
	KUNIT_CASE(nommu_swmmu_mm_fork_zalloc_rollback_test),
	KUNIT_CASE(nommu_swmmu_mm_fork_page_alloc_rollback_test),
	KUNIT_CASE(nommu_swmmu_mm_map_fixed_replace_test),
	KUNIT_CASE(nommu_swmmu_mm_map_fixed_head_replace_test),
	KUNIT_CASE(nommu_swmmu_mm_map_fixed_head_replace_rollback_test),
	KUNIT_CASE(nommu_swmmu_mm_map_fixed_tail_replace_test),
	{}
};



static struct kunit_suite nommu_swmmu_test_suite = {
	.name = "nommu-swmmu",
	.init = nommu_swmmu_test_init,
	.exit = nommu_swmmu_test_exit,
	.test_cases = nommu_swmmu_test_cases,
};
kunit_test_suite(nommu_swmmu_test_suite);

static struct kunit_suite nommu_swmmu_mm_test_suite = {
	.name = "nommu-swmmu-mm",
	.init = nommu_swmmu_mm_ctx_init,
	.exit = nommu_swmmu_mm_ctx_exit,
	.test_cases = nommu_swmmu_mm_test_cases,
};
kunit_test_suite(nommu_swmmu_mm_test_suite);
MODULE_LICENSE("GPL");
