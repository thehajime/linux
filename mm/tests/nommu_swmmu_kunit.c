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

static int nommu_swmmu_mm_ctx_init(struct kunit *test)
{
	struct nommu_swmmu_mm_test_ctx *ctx;
	struct nommu_swmmu_space *old_space;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

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
