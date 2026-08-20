#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <kunit/uapi.h>

static void test_mcontext(struct kunit *test)
{
	KUNIT_UAPI_EMBED_BLOB(test_fp_save_restore, "test-fp-save-restore");

	kunit_uapi_run_kselftest(test, &test_fp_save_restore);
}

static struct kunit_case register_test_cases[] = {
	KUNIT_CASE(test_mcontext),
	{}
};

static struct kunit_suite register_test_suite = {
	.name = "um_registers",
	.test_cases = register_test_cases,
};

kunit_test_suites(&register_test_suite);
