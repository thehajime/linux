/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_UM_SETJMP_H
#define __ARCH_UM_SETJMP_H

struct __jmp_buf {
	unsigned long __dummy;
};
#define JB_IP __dummy
#define JB_SP __dummy

typedef struct __jmp_buf jmp_buf[1];

#endif /* __ARCH_UM_SETJMP_H */
