/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/cache.h>
#include <linux/export.h>

#undef INIT_CALLS_LEVEL
#define INIT_CALLS_LEVEL(level)						\
	.initcall##level##.init : {					\
		__initcall##level##_start = .;				\
		KEEP(*(.initcall##level##.init))			\
		KEEP(*(.initcall##level##s.init))			\
	}								\

#undef INIT_CALLS
#define INIT_CALLS						\
	.initcall.start : {					\
		__initcall_start = .;				\
		KEEP(*(.initcallearly.init))			\
	}							\
	INIT_CALLS_LEVEL(0)					\
	INIT_CALLS_LEVEL(1)					\
	INIT_CALLS_LEVEL(2)					\
	INIT_CALLS_LEVEL(3)					\
	INIT_CALLS_LEVEL(4)					\
	INIT_CALLS_LEVEL(5)					\
	INIT_CALLS_LEVEL(rootfs)				\
	INIT_CALLS_LEVEL(6)					\
	INIT_CALLS_LEVEL(7)					\
	__initcall_end = .;

#define UM_INIT_DATA_SECTION(initsetup_align)				\
	.init.data : AT(ADDR(.init.data) - LOAD_OFFSET) {		\
		INIT_DATA						\
		INIT_SETUP(initsetup_align)				\
	}								\
	INIT_CALLS						\
	.init.data2 : AT(ADDR(.init.data2) - LOAD_OFFSET) {		\
		CON_INITCALL						\
		INIT_RAM_FS						\
	}								\

OUTPUT_FORMAT(ELF_FORMAT)
jiffies = jiffies_64;


SECTIONS
{
	__init_begin = . ;
	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	UM_INIT_DATA_SECTION(16)
	PERCPU_SECTION(L1_CACHE_BYTES)
	__init_end = . ;

	_stext = . ;
	_text = . ;
	.text : ALIGN(THREAD_SIZE)
	{
		__binary_start = . ;
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		CPUIDLE_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
	}
	_etext = . ;

	.syscall_stub : ALIGN(PAGE_SIZE)
	{
		__syscall_stub_start = .;
		*(.__syscall_stub*)
		__syscall_stub_end = .;
	}

	_sdata = . ;
	RO_DATA(PAGE_SIZE)
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	.data : ALIGN(THREAD_SIZE)  { }

	EXCEPTION_TABLE(16)

	.uml.init_irqstack : ALIGN(THREAD_SIZE) {
		*(.data..init_irqstack)
	}

	.uml.setup.init : {
		__uml_setup_start = .;
		*(.uml.setup.init)
		__uml_setup_end = .;
	}

	.uml.help.init : {
		__uml_help_start = .;
		*(.uml.help.init)
		__uml_help_end = .;
	}

	.uml.postsetup.init : {
		__uml_postsetup_start = .;
		*(.uml.postsetup.init)
		__uml_postsetup_end = .;
	}

	.uml.exitcall : {
		__uml_exitcall_begin = .;
		*(.uml.exitcall.exit)
		__uml_exitcall_end = .;
	}
	_edata = . ;

	BSS_SECTION(0, 0, 0)
	_end = . ;

	STABS_DEBUG

	DWARF_DEBUG

	DISCARDS
}
