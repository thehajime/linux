/* SPDX-License-Identifier: GPL-2.0 */
#include <asm-generic/vmlinux.lds.h>

  .fini      : { *(.fini)    } =0x9090
  _etext = .;
  PROVIDE (etext = .);

  . = ALIGN(4096);
  _sdata = .;
  PROVIDE (sdata = .);

  RO_DATA(4096)

  .unprotected : { *(.unprotected) }
  . = ALIGN(4096);
  PROVIDE (_unprotected_end = .);

  . = ALIGN(4096);
  EXCEPTION_TABLE(0)

  BUG_TABLE

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
	
  .init.setup : {
	INIT_SETUP(0)
  }

  PERCPU_SECTION(32)
	
  .initcall.init : {
	__initcall_start = .;
	KEEP(*(.initcallearly.init))
  }
  .initcall0.init : {
	INIT_CALLS_LEVEL(0)
  }
  .initcall1.init : {
	INIT_CALLS_LEVEL(1)
  }
  .initcall2.init : {
	INIT_CALLS_LEVEL(2)
  }
  .initcall3.init : {
	INIT_CALLS_LEVEL(3)
  }
  .initcall4.init : {
	INIT_CALLS_LEVEL(4)
  }
  .initcall5.init : {
	INIT_CALLS_LEVEL(5)
  }
  .initcallrootfs.init : {
	INIT_CALLS_LEVEL(rootfs)
  }
  .initcall6.init : {
	INIT_CALLS_LEVEL(6)
  }
  .initcall7.init : {
	INIT_CALLS_LEVEL(7)
  }
  __initcall_end = .;

  .con_initcall.init : {
	CON_INITCALL
  }

  .exitcall : {
	__exitcall_begin = .;
	*(.exitcall.exit)
	__exitcall_end = .;
  }

  .uml.exitcall : {
	__uml_exitcall_begin = .;
	*(.uml.exitcall.exit)
	__uml_exitcall_end = .;
  }

  . = ALIGN(4);
  .altinstructions : {
	__alt_instructions = .;
	*(.altinstructions)
	__alt_instructions_end = .;
  }
  .altinstr_replacement : { *(.altinstr_replacement) }
  /* .exit.text is discard at runtime, not link time, to deal with references
     from .altinstructions and .eh_frame */
  .exit.text : { EXIT_TEXT }
  .exit.data : { *(.exit.data) }

  .preinit_array : {
	__preinit_array_start = .;
	*(.preinit_array)
	__preinit_array_end = .;
  }
  .init_array : {
	__init_array_start = .;
	*(.init_array)
	__init_array_end = .;
  }
  .fini_array : {
	__fini_array_start = .;
	*(.fini_array)
	__fini_array_end = .;
  }

   . = ALIGN(4096);
  .init.ramfs : {
	INIT_RAM_FS
  }

