#include <linux/module.h>
#include <linux/sched.h>
#include <asm/unistd.h>
#include <asm/insn.h>
#include <os.h>

extern long __kernel_vsyscall(int64_t, int64_t, int64_t, int64_t,
			      int64_t, int64_t, int64_t);
/* start of trampoline code area */
static char *__zpoline_start;


long zpoline_syscall_hook(int64_t rdi, int64_t rsi,
				 int64_t rdx, int64_t __rcx __attribute__((unused)),
				 int64_t r8, int64_t r9,
				 int64_t r10_on_stack /* 4th arg for syscall */,
				 int64_t rax_on_stack,
				 int64_t retptr)
{
	unsigned long ret = -1;

	if (rax_on_stack == __NR_clone3) {
		uint64_t *ca = (uint64_t *) rdi; /* struct clone_args */
		if (ca[0] /* flags */ & CLONE_VM) {
			ca[6] /* stack_size */ -= sizeof(uint64_t);
			*((uint64_t *) (ca[5] /* stack */ + ca[6] /* stack_size */)) = retptr;
		}
	}

	if (rax_on_stack == __NR_clone) {
		if (rdi & CLONE_VM) { // pthread creation
			/* push return address to the stack */
			rsi -= sizeof(uint64_t);
			*((uint64_t *) rsi) = retptr;
		}
	}

	/* XXX: r9... */
	__asm__ __volatile__ ("call *%1" : "=a"(ret)
			      : "r"(__kernel_vsyscall), "a"(rax_on_stack),
				"D"(rdi), "S"(rsi), "d"(rdx),
				"r"(r10_on_stack), "r"(r8)//, "r"(_r9)
			      : "rcx", "r11", "memory");

	return ret;
}

void ____asm_impl(void)
{
	/*
	 * enter_syscall triggers a kernel-space system call
	 */
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

	/*
	 * asm_syscall_hook is the address where the
	 * trampoline code first lands.
	 *
	 * the procedure below calls the C function
	 * named syscall_hook.
	 *
	 * at the entry point of this,
	 * the register values follow the calling convention
	 * of the system calls.
	 *
	 * this part is a bit complicated.
	 * commit e5afaba has a bit simpler versoin.
	 *
	 */
	asm volatile (
	".globl asm_syscall_hook \n\t"
	"asm_syscall_hook: \n\t"

#if 0
	"cmpq $15, %rax \n\t" // rt_sigreturn
	"je do_rt_sigreturn \n\t"
#endif
	"pushq %rbp \n\t"
	"movq %rsp, %rbp \n\t"
	/*
	 * NOTE: for xmm register operations such as movaps
	 * stack is expected to be aligned to a 16 byte boundary.
	 */
	"andq $-16, %rsp \n\t" // 16 byte stack alignment

	/* assuming callee preserves r12-r15 and rbx  */

	"pushq %r11 \n\t"
	"pushq %r9 \n\t"
	"pushq %r8 \n\t"
	"pushq %rdi \n\t"
	"pushq %rsi \n\t"
	"pushq %rdx \n\t"
	"pushq %rcx \n\t"

	/* arguments for syscall_hook */

	"pushq 8(%rbp) \n\t"	// return address
	"pushq %rax \n\t"
	"pushq %r10 \n\t"

	/* up to here, stack has to be 16 byte aligned */
	"callq zpoline_syscall_hook \n\t"
//	"callq __kernel_vsyscall \n\t"

	"popq %r10 \n\t"
	"addq $16, %rsp \n\t"	// discard arg7 and arg8

	"popq %rcx \n\t"
	"popq %rdx \n\t"
	"popq %rsi \n\t"
	"popq %rdi \n\t"
	"popq %r8 \n\t"
	"popq %r9 \n\t"
	"popq %r11 \n\t"

	"leaveq \n\t"

//	"addq $128, %rsp \n\t"

	"retq \n\t"

#if 0
	"do_rt_sigreturn:"
	"addq $136, %rsp \n\t"
	"jmp syscall_addr \n\t"
#endif
	);
}

int arch_finalize_exec(struct elfhdr *, bool, struct elfhdr *);
int arch_finalize_exec(struct elfhdr *_ehdr, bool has_interp,
			struct elfhdr *_interp_ehdr)
{
	int err = 0, count = 0;
	struct insn insn;
	struct mm_struct *mm = current->mm;
	void *ptr, *head;
	unsigned long stop;
	struct elfhdr *ehdr = _interp_ehdr;

	ptr = (void *)_interp_ehdr;
	head = ptr;
	stop = ehdr->e_shoff + ehdr->e_shentsize * ehdr->e_shnum;

	/* skip translation of trampoline code */
	if (ptr <= (void *)&__zpoline_start[0] + 0x1000 + 0x0100)
		return - EINVAL;

	if (down_write_killable(&mm->mmap_lock))
		return -EINTR;


	while (ptr < (head + stop)) {
		insn_init(&insn, ptr, MAX_INSN_SIZE, 1);
		insn_get_length(&insn);

		insn_get_opcode(&insn);

//		printk(KERN_INFO "zpoline: %lx: looking for\n", (unsigned long)ptr);
		switch (insn.opcode.bytes[0]) {
		case 0xf:
			switch (insn.opcode.bytes[1]) {
			case 0x05: /* syscall */
			case 0x34: /* sysenter */
				//printk(KERN_INFO "zpoline: %lx: found syscall/sysenter\n", (unsigned long)ptr);
				*(char*)ptr = 0xff; // callq
				*((char *)ptr + 1) = 0xd0; // *%rax
				count++;
				break;
			}
		default:
		}

		ptr += insn.length;
		if (insn.length == 0) {
			//printk(KERN_INFO "zpoline: %lx: length zero with byte %lx. skip ?\n",
			//       (unsigned long)ptr, insn.opcode.bytes[0]);
			ptr += 1;
		}
	}

	printk(KERN_DEBUG "zpoline: rewritten %d syscalls\n", count);
	up_write(&mm->mmap_lock);
	return err;
}

static int setup_zpoline_trampoline(void)
{
	extern void asm_syscall_hook(void);
	int i, ret;

	__zpoline_start = 0x0;

	ret = os_map_memory((void *) 0, -1, 0, 0x1000, 1, 1, 1);
	if (ret) {
		panic("map failed\n NOTE: /proc/sys/vm/mmap_min_addr should be set 0\n");
	}

	for (i = 0; i < NR_syscalls; i++)
		__zpoline_start[i] = 0x90;

	// optimization introduced by reviewer C
	__zpoline_start[214 /* __NR_epoll_ctl_old */] = 0xeb; /* short jmp */
	__zpoline_start[215 /* __NR_epoll_wait_old */] = 127; /* range of a short jmp : -128 ~ +127 */

	/* 
	 * put code for jumping to asm_syscall_hook.
	 *
	 * here we embed the following code.
	 *
	 * //sub    $0x80,%rsp
	 * movabs [asm_syscall_hook],%r11
	 * jmpq   *%r11
	 *
	 */

#if 0
	/* preserve redzone */
	// 48 81 ec 80 00 00 00    sub    $0x80,%rsp
	__zpoline_start[NR_syscalls + 0x00] = 0x48;
	__zpoline_start[NR_syscalls + 0x01] = 0x81;
	__zpoline_start[NR_syscalls + 0x02] = 0xec;
	__zpoline_start[NR_syscalls + 0x03] = 0x80;
	__zpoline_start[NR_syscalls + 0x04] = 0x00;
	__zpoline_start[NR_syscalls + 0x05] = 0x00;
	__zpoline_start[NR_syscalls + 0x06] = 0x00;

	// 49 bb [64-bit addr (8-byte)]    movabs [64-bit addr (8-byte)],%r11
	__zpoline_start[NR_syscalls + 0x00] = 0x49;
	__zpoline_start[NR_syscalls + 0x01] = 0xbb;
	__zpoline_start[NR_syscalls + 0x02] = ((uint64_t) asm_syscall_hook >> (8 * 0)) & 0xff;
	__zpoline_start[NR_syscalls + 0x03] = ((uint64_t) asm_syscall_hook >> (8 * 1)) & 0xff;
	__zpoline_start[NR_syscalls + 0x04] = ((uint64_t) asm_syscall_hook >> (8 * 2)) & 0xff;
	__zpoline_start[NR_syscalls + 0x05] = ((uint64_t) asm_syscall_hook >> (8 * 3)) & 0xff;
	__zpoline_start[NR_syscalls + 0x06] = ((uint64_t) asm_syscall_hook >> (8 * 4)) & 0xff;
	__zpoline_start[NR_syscalls + 0x07] = ((uint64_t) asm_syscall_hook >> (8 * 5)) & 0xff;
	__zpoline_start[NR_syscalls + 0x08] = ((uint64_t) asm_syscall_hook >> (8 * 6)) & 0xff;
	__zpoline_start[NR_syscalls + 0x09] = ((uint64_t) asm_syscall_hook >> (8 * 7)) & 0xff;

	// 41 ff e3                jmp    *%r11
	__zpoline_start[NR_syscalls + 0x0a] = 0x41;
	__zpoline_start[NR_syscalls + 0x0b] = 0xff;
	__zpoline_start[NR_syscalls + 0x0c] = 0xe3;
#else
	// 49 bb [64-bit addr (8-byte)]    movabs [64-bit addr (8-byte)],%r11
	__zpoline_start[NR_syscalls + 0x00] = 0x49;
	__zpoline_start[NR_syscalls + 0x01] = 0xbb;
	__zpoline_start[NR_syscalls + 0x02] = ((uint64_t) __kernel_vsyscall >> (8 * 0)) & 0xff;
	__zpoline_start[NR_syscalls + 0x03] = ((uint64_t) __kernel_vsyscall >> (8 * 1)) & 0xff;
	__zpoline_start[NR_syscalls + 0x04] = ((uint64_t) __kernel_vsyscall >> (8 * 2)) & 0xff;
	__zpoline_start[NR_syscalls + 0x05] = ((uint64_t) __kernel_vsyscall >> (8 * 3)) & 0xff;
	__zpoline_start[NR_syscalls + 0x06] = ((uint64_t) __kernel_vsyscall >> (8 * 4)) & 0xff;
	__zpoline_start[NR_syscalls + 0x07] = ((uint64_t) __kernel_vsyscall >> (8 * 5)) & 0xff;
	__zpoline_start[NR_syscalls + 0x08] = ((uint64_t) __kernel_vsyscall >> (8 * 6)) & 0xff;
	__zpoline_start[NR_syscalls + 0x09] = ((uint64_t) __kernel_vsyscall >> (8 * 7)) & 0xff;

	// 41 ff e3                jmp    *%r11
	__zpoline_start[NR_syscalls + 0x0a] = 0x41;
	__zpoline_start[NR_syscalls + 0x0b] = 0xff;
	__zpoline_start[NR_syscalls + 0x0c] = 0xe3;
#endif

	os_protect_memory(0, 0x1000, 1, 0, 1);
	printk(KERN_ERR "zpoline: setting up trampoline code done\n");
	return 0;
}
__initcall(setup_zpoline_trampoline);
