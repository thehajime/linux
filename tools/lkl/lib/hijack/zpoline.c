/*
 * zpoline related code for hijack
 * Copyright (c) 202 Hajime Tazaki
 *
 * Author: Hajime Tazaki <thehajime@gmail.com>
 *
 * Note: https://github.com/yasukata/zpoline
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <dis-asm.h>
#include <sched.h>

#define NR_syscalls (1024)	/* XXX */

#include <lkl.h>
#include <lkl_host.h>
#include "init.h"
#include "xlate.h"

extern void syscall_addr(void);
extern long enter_syscall(long number, ...);
extern void asm_syscall_hook(void);

static int lkl_call(int nr, ...)
{
	long params[6];
	va_list vl;
	int i;

	va_start(vl, nr);
	for (i = 0; i < 6; i++)
		params[i] = va_arg(vl, long);
	va_end(vl);

	return lkl_set_errno(lkl_syscall(nr, params));
}

long syscall_hook(int64_t a1, int64_t a2, int64_t a3,
		  int64_t a4, int64_t a5, int64_t a6,
		  int64_t a7, int64_t retptr)
{
	int ret;

	if (!lkl_running) {
		if (a1 == __NR_clone) {
			if (a2 & CLONE_VM) { // pthread creation
				/* push return address to the stack */
				a3 -= sizeof(uint64_t);
				*((uint64_t *) a3) = retptr;
			}
		}
		return enter_syscall(a1, a2, a3, a4, a5, a6);
	}

#define CALL_LKL_SYSCALL(x)						\
	case __NR_##x:							\
		ret = lkl_call(__lkl__NR_##x, a2, a3, a4, a5, a6);	\
		break;

	switch (a1) {
		CALL_LKL_SYSCALL(sendmsg);
		CALL_LKL_SYSCALL(recvmsg);
		CALL_LKL_SYSCALL(socket);
		CALL_LKL_SYSCALL(bind);
		CALL_LKL_SYSCALL(connect);
		CALL_LKL_SYSCALL(setsockopt);
		CALL_LKL_SYSCALL(getsockopt);
		CALL_LKL_SYSCALL(getsockname);
		CALL_LKL_SYSCALL(sendto);
		CALL_LKL_SYSCALL(listen);
		CALL_LKL_SYSCALL(accept);
		CALL_LKL_SYSCALL(close);
		CALL_LKL_SYSCALL(ioctl);
		CALL_LKL_SYSCALL(fcntl);
		CALL_LKL_SYSCALL(read);
	default:
		return enter_syscall(a1, a2, a3, a4, a5, a6);
		break;
	}

	if (ret == LKL_ENOSYS) {
		printf("no syscall defined in LKL\n");
	}

	return ret;
}

void ___enter_syscall(void)
{
	asm volatile (
	".globl enter_syscall \n\t"
	"enter_syscall: \n\t"
	"movq %rdi, %rax \n\t"
	"movq %rsi, %rdi \n\t"
	"movq %rdx, %rsi \n\t"
	"movq %rcx, %rdx \n\t"
	"movq %r8, %r10 \n\t"
	"movq %r9, %r8 \n\t"
	"movq 8(%rsp),%r9 \n\t"
	".globl syscall_addr \n\t"
	"syscall_addr: \n\t"
	"syscall \n\t"
	"ret \n\t"
	);
}

struct disassembly_state {
	char *code;
	size_t off;
};

/*
 * this actually rewrites the code.
 * this is called by the disassembler.
 */
static int fprintf_fn(void *data, const char *fmt, ...) {
	struct disassembly_state *s = (struct disassembly_state *) data;
	char buf[4096];
	va_list arg;
	va_start(arg, fmt);
	vsprintf(buf, fmt, arg);
	/* replace syscall and sysenter with callq *%rax */
	if (!strncmp(buf, "syscall", 7) || !strncmp(buf, "sysenter", 8)) {
		uint8_t *ptr = (uint8_t *)(((uintptr_t) s->code) + s->off);
		if ((uintptr_t) ptr == (uintptr_t) syscall_addr) {
			/*
			 * skip the syscall replacement for
			 * our system call hook (enter_syscall)
			 * so that it can issue system calls.
			 */
			goto skip;
		}
		ptr[0] = 0xff; // callq
		ptr[1] = 0xd0; // *%rax
	}
skip:
	va_end(arg);
	return 0;
}

/* find syscall and sysenter using the disassembler, and rewrite them */
static void disassemble_and_rewrite(char *code, size_t code_size, int mem_prot)
{
	struct disassembly_state s = { 0 };
	/* add PROT_WRITE to rewrite the code */
	assert(!mprotect(code, code_size, PROT_WRITE | PROT_READ | PROT_EXEC));
	disassemble_info disasm_info = { 0 };
	init_disassemble_info(&disasm_info, &s, fprintf_fn);
	disasm_info.arch = bfd_arch_i386;
	disasm_info.mach = bfd_mach_x86_64;
	disasm_info.buffer = (bfd_byte *) code;
	disasm_info.buffer_length = code_size;
	disassemble_init_for_target(&disasm_info);
	disassembler_ftype disasm;
	disasm = disassembler(bfd_arch_i386, false, bfd_mach_x86_64, NULL);
	s.code = code;
	while (s.off < code_size)
		s.off += disasm(s.off, &disasm_info);
	/* restore the memory protection */
	assert(!mprotect(code, code_size, mem_prot));
}

/* entry point for binary rewriting */
static void rewrite_code(void)
{
	FILE *fp;
	char buf[4096];

	/* get memory mapping information from procfs */
	assert((fp = fopen("/proc/self/maps", "r")) != NULL);

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		char addr[65];
		int i = 0;
		size_t j;
		char *c;

		/* we do not touch stack and vsyscall memory */
		if (strstr(buf, "stack") != NULL || strstr(buf, "vsyscall") != NULL)
			continue;

		c = strtok(buf, " ");

		while (c != NULL) {
			switch (i) {
			case 0:
				strncpy(addr, c, sizeof(addr) - 1);
				break;
			case 1:
				{
					int mem_prot = 0;
					for (j = 0; j < strlen(c); j++) {
						if (c[j] == 'r')
							mem_prot |= PROT_READ;
						if (c[j] == 'w')
							mem_prot |= PROT_WRITE;
						if (c[j] == 'x')
							mem_prot |= PROT_EXEC;
					}
					/* rewrite code if the memory is executable */
					if (mem_prot & PROT_EXEC) {
						size_t k;
						int64_t from, to;
						for (k = 0; k < strlen(addr); k++) {
							if (addr[k] == '-') {
								addr[k] = '\0';
								break;
							}
						}
						from = strtol(&addr[0], NULL, 16);
						if (from == 0) {
							/*
							 * this is trampoline code.
							 * so skip it.
							 */
							break;
						}
						to = strtol(&addr[k+1], NULL, 16);
						disassemble_and_rewrite((char *) from,
								(size_t) to - from,
								mem_prot);
						break;
					}
				}
				break;
			}
			if (i == 1)
				break;
			c = strtok(NULL, " ");
			i++;
		}
	}

	fclose(fp);
}

void ____asm_syscall_hook(void)
{
	/*
	 * asm_syscall_hook is the address where the
	 * trampoline code first jumps to.
	 *
	 * the procedure below calls the C function
	 * namded syscall_hook.
	 *
	 * at the entry point of this,
	 * the register values follow the calling convention
	 * of the system calls. the following  transforms
	 * to the calling convention of the C functions.
	 *
	 * we do this just for writing the hook in C.
	 * so, this part would not be performance optimal.
	 *
	 */
	asm volatile (
	".globl asm_syscall_hook \n\t"
	"asm_syscall_hook: \n\t"
	"popq %rax \n\t" /* restore %rax saved in the trampoline code */
	"movq (%rsp), %rcx \n\t"
	"subq $16,%rsp \n\t"
	"movq %rcx,8(%rsp) \n\t"
	"movq %r9,(%rsp) \n\t"
	"movq %r8, %r9 \n\t"
	"movq %r10, %r8 \n\t"
	"movq %rdx, %rcx \n\t"
	"movq %rsi, %rdx \n\t"
	"movq %rdi, %rsi \n\t"
	"movq %rax, %rdi \n\t"
	"call syscall_hook \n\t"
	"addq $16,%rsp \n\t"
	"retq \n\t"
	);
}

static void setup_trampoline(void)
{
	void *mem;

	/* allocate memory at virtual address 0 */
	mem = mmap(0 /* virtual address 0 */, 0x1000,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
			-1, 0);
	if (mem == MAP_FAILED) {
		perror("mmap failed");
		printf("NOTE: /proc/sys/vm/mmap_min_addr should be set 0\n");
		exit(1);
	}

	/* fill with nop 0x90 */
	memset(mem, 0x90, NR_syscalls);

	/* 
	 * put code for jumping to asm_syscall_hook.
	 *
	 * here we embed the following code which jumps
	 * to the address written on 0xff8
	 *
	 * push   %rax
	 * mov    $0xff8,%rax
	 * jmpq   *(%rax)
	 *
	 */

	/*
	 * save %rax on stack before overwriting
	 * with "mov $0xff8,%rax",
	 * and the saved value is resumed in asm_syscall_hook.
	 */
	// 50                      push   %rax
	((uint8_t *) mem)[NR_syscalls + 0] = 0x50;

	// 48 c7 c0 f8 0f 00 00    mov    $0xff8,%rax
	((uint8_t *) mem)[NR_syscalls + 1] = 0x48;
	((uint8_t *) mem)[NR_syscalls + 2] = 0xc7;
	((uint8_t *) mem)[NR_syscalls + 3] = 0xc0;
	((uint8_t *) mem)[NR_syscalls + 4] = 0xf8;
	((uint8_t *) mem)[NR_syscalls + 5] = 0x0f;
	((uint8_t *) mem)[NR_syscalls + 6] = 0x00;
	((uint8_t *) mem)[NR_syscalls + 7] = 0x00;

	// ff 20                   jmpq   *(%rax)
	((uint8_t *) mem)[NR_syscalls + 8] = 0xff;
	((uint8_t *) mem)[NR_syscalls + 9] = 0x20;

	/* finally, this sets the address of asm_syscall_hook at 0xff8 */
	*(uint64_t *)(&((uint8_t *) mem)[0xff8]) = (uint64_t) asm_syscall_hook;
}

void __zpoline_init(void)
{
	printf("Initializing Zpoline ...\n");

	printf("-- Setting up trampoline code\n"); fflush(stdout);
	setup_trampoline();

	printf("-- Rewriting the code\n"); fflush(stdout);
	rewrite_code();

	printf("Zpoline initialization OK\n");
	printf("Start main program\n");
}
