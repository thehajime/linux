/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/vmlinux.lds.h>
#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/cache.h>
#include <linux/export.h>

OUTPUT_FORMAT(ELF_FORMAT)
jiffies = jiffies_64;


SECTIONS
{
	__init_begin = . ;
	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	INIT_DATA_SECTION(16)
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

	.uml.setup.init : ALIGN(8) {
		__uml_setup_start = .;
		*(.uml.setup.init)
		__uml_setup_end = .;
	}

	.uml.help.init : ALIGN(8)  {
		__uml_help_start = .;
		*(.uml.help.init)
		__uml_help_end = .;
	}

	.uml.postsetup.init : ALIGN(8) {
		__uml_postsetup_start = .;
		*(.uml.postsetup.init)
		__uml_postsetup_end = .;
	}

	.uml.exitcall : ALIGN(8) {
		__uml_exitcall_begin = .;
		*(.uml.exitcall.exit)
		__uml_exitcall_end = .;
	}
	_edata = . ;

	BSS_SECTION(0, 0, 0)
	_end = . ;

	STABS_DEBUG

	DWARF_DEBUG
	ELF_DETAILS

	DISCARDS
}
