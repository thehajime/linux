// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Anton Ivanov (aivanov@{brocade.com,kot-begemot.co.uk})
 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright 2003 PathScale, Inc.
 */

#include <linux/stddef.h>
#include <linux/err.h>
#include <linux/hardirq.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/personality.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/random.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/seq_file.h>
#include <linux/tick.h>
#include <linux/threads.h>
#include <linux/resume_user_mode.h>
#include <asm/current.h>
#include <asm/mmu_context.h>
#include <asm/switch_to.h>
#include <asm/exec.h>
#include <linux/uaccess.h>
#include <as-layout.h>
#include <kern_util.h>
#include <os.h>
#include <skas.h>
#include <registers.h>
#include <linux/time-internal.h>
#include <linux/elfcore.h>
#include <linux/nommu_swmmu.h>

/*
 * This is a per-cpu array.  A processor only modifies its entry and it only
 * cares about its entry, so it's OK if another processor is modifying its
 * entry.
 */
struct task_struct *cpu_tasks[NR_CPUS] = {
	[0 ... NR_CPUS - 1] = &init_task,
};
EXPORT_SYMBOL(cpu_tasks);

void free_stack(unsigned long stack, int order)
{
	free_pages(stack, order);
}

unsigned long alloc_stack(int order, int atomic)
{
	unsigned long page;
	gfp_t flags = GFP_KERNEL;

	if (atomic)
		flags = GFP_ATOMIC;
	page = __get_free_pages(flags, order);

	return page;
}

static inline void set_current(struct task_struct *task)
{
	cpu_tasks[task_thread_info(task)->cpu] = task;
}

struct task_struct *__switch_to(struct task_struct *from, struct task_struct *to)
{
	to->thread.prev_sched = from;
	set_current(to);

	switch_threads(&from->thread.switch_buf, &to->thread.switch_buf);
	arch_switch_to(current);

	return current->thread.prev_sched;
}

void interrupt_end(void)
{
	struct pt_regs *regs = &current->thread.regs;
	unsigned long thread_flags;

	thread_flags = read_thread_flags();
	while (thread_flags & _TIF_WORK_MASK) {
		if (thread_flags & _TIF_NEED_RESCHED)
			schedule();
		if (thread_flags & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL))
			do_signal(regs);
		if (thread_flags & _TIF_NOTIFY_RESUME)
			resume_user_mode_work(regs);
		thread_flags = read_thread_flags();
	}
}

int get_current_pid(void)
{
	return task_pid_nr(current);
}

/*
 * This is called magically, by its address being stuffed in a jmp_buf
 * and being longjmp-d to.
 */
void new_thread_handler(void)
{
	int (*fn)(void *);
	void *arg;

	if (current->thread.prev_sched != NULL)
		schedule_tail(current->thread.prev_sched);
	current->thread.prev_sched = NULL;

	fn = current->thread.request.thread.proc;
	arg = current->thread.request.thread.arg;

	/*
	 * callback returns only if the kernel thread execs a process
	 */
	fn(arg);
	userspace(&current->thread.regs.regs);
}

static int uml_nommu_get_user_stack_range(struct mm_struct *mm,
				   unsigned long sp,
				   unsigned long *low,
					unsigned long *high);
static void log_all_register(unsigned long stack_low, unsigned long stack_high);

/* Called magically, see new_thread_handler above */
static void fork_handler(void)
{
	schedule_tail(current->thread.prev_sched);

	/*
	 * XXX: if interrupt_end() calls schedule, this call to
	 * arch_switch_to isn't needed. We could want to apply this to
	 * improve performance. -bb
	 */
	arch_switch_to(current);

	current->thread.prev_sched = NULL;

	userspace(&current->thread.regs.regs);
}

struct uml_nommu_user_stack {
	void *base;
	unsigned long requested_size;
	unsigned long allocated_size;
	unsigned int order;
};

static struct uml_nommu_user_stack *
uml_nommu_user_stack_alloc(unsigned long requested_size)
{
	struct uml_nommu_user_stack *stack;
	unsigned long allocated_size;
	unsigned int order;

	if (!requested_size)
		return NULL;

	order = get_order(PAGE_ALIGN(requested_size));
	allocated_size = PAGE_SIZE << order;

	if (allocated_size < requested_size)
		return NULL;

	stack = kzalloc(sizeof(*stack), GFP_KERNEL);
	if (!stack)
		return NULL;

	stack->base = (void *)alloc_stack(order,
					  __uml_cant_sleep());
	if (!stack->base) {
		kfree(stack);
		return NULL;
	}

	if (!IS_ALIGNED((unsigned long)stack->base, PAGE_SIZE)) {
		pr_err("UML user stack is not page-aligned: %px\n",
		       stack->base);
		free_stack((unsigned long)stack->base, order);
		kfree(stack);
		return NULL;
	}

	stack->requested_size = requested_size;
	stack->allocated_size = allocated_size;
	stack->order = order;

	return stack;
}

static void
uml_nommu_user_stack_free(struct uml_nommu_user_stack *stack)
{
	if (!stack)
		return;

	free_stack((unsigned long)stack->base,
		   stack->order);
	kfree(stack);
}

static int
uml_nommu_get_user_stack_range(struct mm_struct *mm,
			       unsigned long sp,
			       unsigned long *low,
			       unsigned long *high)
{
	if (!mm || !low || !high)
		return -EINVAL;

	if (!mm->start_brk ||
	    mm->start_stack <= mm->start_brk)
		return -EINVAL;

	if (sp < mm->start_brk ||
	    sp > mm->start_stack)
		return -EFAULT;

	*low = mm->start_brk;
	*high = mm->start_stack;

	return 0;
}

static unsigned long
relocate_stack_pointer(unsigned long pointer,
		       unsigned long old_low,
		       unsigned long old_high,
		       unsigned long new_low)
{
	if (pointer < old_low || pointer >= old_high)
		return pointer;

	return new_low + (pointer - old_low);
}

static void
uml_nommu_relocate_stack_registers(struct uml_pt_regs *regs,
				   unsigned long old_low,
				   unsigned long old_high,
				   unsigned long new_low)
{
	unsigned long *gp = regs->gp;

	gp[HOST_BP] = relocate_stack_pointer(
		gp[HOST_BP], old_low, old_high, new_low);

	gp[HOST_BX] = relocate_stack_pointer(
		gp[HOST_BX], old_low, old_high, new_low);

	gp[HOST_CX] = relocate_stack_pointer(
		gp[HOST_CX], old_low, old_high, new_low);

	gp[HOST_DX] = relocate_stack_pointer(
		gp[HOST_DX], old_low, old_high, new_low);

	gp[HOST_SI] = relocate_stack_pointer(
		gp[HOST_SI], old_low, old_high, new_low);

	gp[HOST_DI] = relocate_stack_pointer(
		gp[HOST_DI], old_low, old_high, new_low);

	gp[HOST_R8] = relocate_stack_pointer(
		gp[HOST_R8], old_low, old_high, new_low);

	gp[HOST_R9] = relocate_stack_pointer(
		gp[HOST_R9], old_low, old_high, new_low);

	gp[HOST_R10] = relocate_stack_pointer(
		gp[HOST_R10], old_low, old_high, new_low);

	gp[HOST_R12] = relocate_stack_pointer(
		gp[HOST_R12], old_low, old_high, new_low);

	gp[HOST_R13] = relocate_stack_pointer(
		gp[HOST_R13], old_low, old_high, new_low);

	gp[HOST_R14] = relocate_stack_pointer(
		gp[HOST_R14], old_low, old_high, new_low);

	gp[HOST_R15] = relocate_stack_pointer(
		gp[HOST_R15], old_low, old_high, new_low);
}

static void
uml_nommu_relocate_stack_image(void *new_base,
			       unsigned long size,
			       unsigned long old_low,
			       unsigned long old_high)
{
	unsigned long offset;
	unsigned long new_low = (unsigned long)new_base;

	for (offset = 0;
	     offset + sizeof(unsigned long) <= size;
	     offset += sizeof(unsigned long)) {
		unsigned long *value;

		value = (unsigned long *)((char *)new_base + offset);

		if (*value >= old_low && *value < old_high)
			*value = new_low + (*value - old_low);
	}
}

static void
log_stack_register(const char *name,
		   unsigned long value,
		   unsigned long low,
		   unsigned long high)
{
	if (value >= low && value < high)
		pr_info("SWMMU fork: %s=%lx points into parent stack\n",
			name, value);
}

static void log_all_register(unsigned long stack_low, unsigned long stack_high)
{
	log_stack_register("SP",
			current->thread.regs.regs.gp[HOST_SP],
			stack_low, stack_high);
	log_stack_register("BP",
			current->thread.regs.regs.gp[HOST_BP],
			stack_low, stack_high);
	log_stack_register("BX",
			current->thread.regs.regs.gp[HOST_BX],
			stack_low, stack_high);
	log_stack_register("R12",
			current->thread.regs.regs.gp[HOST_R12],
			stack_low, stack_high);
	log_stack_register("R13",
			current->thread.regs.regs.gp[HOST_R13],
			stack_low, stack_high);
	log_stack_register("R14",
			current->thread.regs.regs.gp[HOST_R14],
			stack_low, stack_high);
	log_stack_register("R15",
			current->thread.regs.regs.gp[HOST_R15],
			stack_low, stack_high);
	log_stack_register("SI",
			current->thread.regs.regs.gp[HOST_SI],
			stack_low, stack_high);
	log_stack_register("DI",
			current->thread.regs.regs.gp[HOST_DI],
			stack_low, stack_high);
}

static int
uml_nommu_copy_user_stack(struct task_struct *child)
{
	struct uml_nommu_user_stack *stack;
	unsigned long parent_sp;
	unsigned long stack_low;
	unsigned long stack_high;
	unsigned long stack_size;
	unsigned long child_stack_base;
	int ret;

	parent_sp = current->thread.regs.regs.gp[HOST_SP];

	ret = uml_nommu_get_user_stack_range(current->mm,
					     parent_sp,
					     &stack_low,
					     &stack_high);
	if (ret)
		return ret;

	stack_size = stack_high - stack_low;

	stack = uml_nommu_user_stack_alloc(stack_size);
	if (!stack)
		return -ENOMEM;

	memcpy(stack->base,
	       (void *)stack_low,
	       stack_size);

	uml_nommu_relocate_stack_image(stack->base,
				       stack_size,
				       stack_low,
				       stack_high);

	child_stack_base = (unsigned long)stack->base;

	child->thread.regs.regs.gp[HOST_SP] =
		child_stack_base + (parent_sp - stack_low);

	uml_nommu_relocate_stack_registers(
		&child->thread.regs.regs,
		stack_low,
		stack_high,
		child_stack_base);

	child->thread.arch.nommu_user_stack = stack;

	return 0;
}

void release_thread(struct task_struct *dead_task)
{
	struct uml_nommu_user_stack *stack;

	stack = dead_task->thread.arch.nommu_user_stack;
	if (!stack)
		return;

	dead_task->thread.arch.nommu_user_stack = NULL;

	uml_nommu_user_stack_free(stack);
}

int copy_thread(struct task_struct * p, const struct kernel_clone_args *args)
{
	u64 clone_flags = args->flags;
	unsigned long sp = args->stack;
	unsigned long tls = args->tls;
	void (*handler)(void);
	int ret = 0;

	p->thread = (struct thread_struct) INIT_THREAD;

	if (!args->fn) {
	  	memcpy(&p->thread.regs.regs, current_pt_regs(),
		       sizeof(p->thread.regs.regs));
		PT_REGS_SET_SYSCALL_RETURN(&p->thread.regs, 0);
		if (sp != 0)
			REGS_SP(p->thread.regs.regs.gp) = sp;

		handler = fork_handler;

		arch_copy_thread(&current->thread.arch, &p->thread.arch);

#ifdef CONFIG_NOMMU_SWMMU
		if (!(args->flags & CLONE_VM)) {
			ret = uml_nommu_copy_user_stack(p);
			if (ret)
				return ret;
		}
#endif
	} else {
		get_safe_registers(p->thread.regs.regs.gp, p->thread.regs.regs.fp);
		p->thread.request.thread.proc = args->fn;
		p->thread.request.thread.arg = args->fn_arg;
		handler = new_thread_handler;
	}

	new_thread(task_stack_page(p), &p->thread.switch_buf, handler);

	if (!args->fn) {
		clear_flushed_tls(p);

		/*
		 * Set a new TLS for the child thread?
		 */
		if (clone_flags & CLONE_SETTLS)
			ret = arch_set_tls(p, tls);
	}

	return ret;
}

void initial_thread_cb(void (*proc)(void *), void *arg)
{
	initial_thread_cb_skas(proc, arg);
}

int arch_dup_task_struct(struct task_struct *dst,
			 struct task_struct *src)
{
	/* init_task is not dynamically sized (missing FPU state) */
	if (unlikely(src == &init_task)) {
		memcpy(dst, src, sizeof(init_task));
		memset((void *)dst + sizeof(init_task), 0,
		       arch_task_struct_size - sizeof(init_task));
	} else {
		memcpy(dst, src, arch_task_struct_size);
	}

	return 0;
}

void um_idle_sleep(void)
{
	if (time_travel_mode != TT_MODE_OFF)
		time_travel_sleep();
	else
		os_idle_sleep();
}

void arch_cpu_idle(void)
{
	um_idle_sleep();
}

void arch_cpu_idle_prepare(void)
{
	os_idle_prepare();
}

int __uml_cant_sleep(void) {
	return in_atomic() || irqs_disabled() || in_interrupt();
	/* Is in_interrupt() really needed? */
}

int uml_need_resched(void)
{
	return need_resched();
}

extern exitcall_t __uml_exitcall_begin, __uml_exitcall_end;

void do_uml_exitcalls(void)
{
	exitcall_t *call;

	call = &__uml_exitcall_end;
	while (--call >= &__uml_exitcall_begin)
		(*call)();
}

char *uml_strdup(const char *string)
{
	return kstrdup(string, GFP_KERNEL);
}
EXPORT_SYMBOL(uml_strdup);

int copy_from_user_proc(void *to, void __user *from, int size)
{
	return copy_from_user(to, from, size);
}

int singlestepping(void)
{
	return test_thread_flag(TIF_SINGLESTEP);
}

/*
 * Only x86 and x86_64 have an arch_align_stack().
 * All other arches have "#define arch_align_stack(x) (x)"
 * in their asm/exec.h
 * As this is included in UML from asm-um/system-generic.h,
 * we can use it to behave as the subarch does.
 */
#ifndef arch_align_stack
unsigned long arch_align_stack(unsigned long sp)
{
	if (!(current->personality & ADDR_NO_RANDOMIZE) && randomize_va_space)
		sp -= get_random_u32_below(8192);
	return sp & ~0xf;
}
#endif

unsigned long __get_wchan(struct task_struct *p)
{
	unsigned long stack_page, sp, ip;
	bool seen_sched = 0;

	stack_page = (unsigned long) task_stack_page(p);
	/* Bail if the process has no kernel stack for some reason */
	if (stack_page == 0)
		return 0;

	sp = p->thread.switch_buf->JB_SP;
	/*
	 * Bail if the stack pointer is below the bottom of the kernel
	 * stack for some reason
	 */
	if (sp < stack_page)
		return 0;

	while (sp < stack_page + THREAD_SIZE) {
		ip = *((unsigned long *) sp);
		if (in_sched_functions(ip))
			/* Ignore everything until we're above the scheduler */
			seen_sched = 1;
		else if (kernel_text_address(ip) && seen_sched)
			return ip;

		sp += sizeof(unsigned long);
	}

	return 0;
}
