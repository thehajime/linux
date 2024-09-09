#include <linux/module.h>
#include <linux/sched.h>
#include <asm/unistd.h>
#include <asm/insn.h>
#include <os.h>

#ifndef CONFIG_MMU
extern long __kernel_vsyscall(int64_t, int64_t, int64_t, int64_t,
			      int64_t, int64_t, int64_t);
/* start of trampoline code area */
static char *__zpoline_start;

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
	if (ptr <= (void *)&__zpoline_start[0] + 0x1000 + 0x0100) {
		err = -EINVAL;
		goto out;
	}

	if (down_write_killable(&mm->mmap_lock)) {
		err = -EINTR;
		goto out;
	}

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

out:
	return err;
}

static int setup_zpoline_trampoline(void)
{
	extern void asm_syscall_hook(void);
	int i, ret;

	/* zpoline: map area of trampoline code started from addr 0x0 */
	__zpoline_start = 0x0;

	ret = os_map_memory((void *) 0, -1, 0, 0x1000, 1, 1, 1);
	if (ret)
		panic("map failed\n NOTE: /proc/sys/vm/mmap_min_addr should be set 0\n");

	for (i = 0; i < NR_syscalls; i++)
		__zpoline_start[i] = 0x90;

	// optimization introduced by reviewer C
	__zpoline_start[214 /* __NR_epoll_ctl_old */] = 0xeb; /* short jmp */
	__zpoline_start[215 /* __NR_epoll_wait_old */] = 127; /* range of a short jmp : -128 ~ +127 */

	/*
	 * put code for jumping to __kernel_vsyscall.
	 *
	 * here we embed the following code.
	 *
	 * movabs [$addr],%r11
	 * jmpq   *%r11
	 *
	 */

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

//#pragma optimize("", off)
	// 41 ff e3                jmp    *%r11
	__zpoline_start[NR_syscalls + 0x0a] = 0x41;
	__zpoline_start[NR_syscalls + 0x0b] = 0xff;
	__zpoline_start[NR_syscalls + 0x0c] = 0xe3;
//#pragma optimize("", on)

	os_protect_memory(0, 0x1000, 1, 0, 1);
	printk(KERN_ERR "zpoline: setting up trampoline code done\n");
	return 0;
}
__initcall(setup_zpoline_trampoline);
#endif /* !CONFIG_MMU */
