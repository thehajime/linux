/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __SYSDEP_X86_64_SIGNAL_H__
#define __SYSDEP_X86_64_SIGNAL_H__

#ifdef CONFIG_MMU
static inline int arch_setup_signal_stack_si(struct rt_sigframe **frame,
					     struct ksignal *ksig)
{
	return 0;
}
static inline struct rt_sigframe *arch_setup_rt_sigreturn(struct rt_sigframe *frame)
{
	return frame;
}
#else
extern int arch_setup_signal_stack_si(struct rt_sigframe **frame,
				      struct ksignal *ksig);
extern struct rt_sigframe *arch_setup_rt_sigreturn(struct rt_sigframe *frame);
#endif

#endif
