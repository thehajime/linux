// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <assert.h>
#include <lkl_host.h>

static void print(const char *str, int len)
{
	int ret __attribute__((unused));

	ret = write(STDOUT_FILENO, str, len);
}

static void panic(void)
{
	assert(0);
}

struct lkl_host_operations lkl_host_ops = {
	.panic = panic,
	.print = print,
	.mem_alloc = (void *)malloc,
	.mem_free = free,
};

