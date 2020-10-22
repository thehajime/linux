// SPDX-License-Identifier: GPL-2.0
#include <setjmp.h>
#include <lkl_host.h>

void lkl_jmp_buf_set(struct lkl_jmp_buf *jmpb, void (*f)(void))
{
	if (!setjmp(*((jmp_buf *)jmpb->buf)))
		f();
}

void lkl_jmp_buf_longjmp(struct lkl_jmp_buf *jmpb, int val)
{
	longjmp(*((jmp_buf *)jmpb->buf), val);
}
