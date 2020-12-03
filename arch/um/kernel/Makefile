# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux,intel}.com)
#

# Don't instrument UML-specific code; without this, we may crash when
# accessing the instrumentation buffer for the first time from the
# kernel.
KCOV_INSTRUMENT                := n

CPPFLAGS_vmlinux.lds := -DSTART=$(LDS_START)		\
                        -DELF_ARCH=$(LDS_ELF_ARCH)	\
                        -DELF_FORMAT=$(LDS_ELF_FORMAT)	\
			$(LDS_EXTRA)
extra-y := vmlinux.lds

obj-y = config.o exitcode.o irq.o ksyms.o \
	process.o reboot.o sigio.o \
	signal.o syscall.o time.o \
	um_arch.o umid.o maccess.o kmsg_dump.o

ifndef CONFIG_UMMODE_LIB
obj-y += exec.o mem.o physmem.o ptrace.o sysrq.o tlb.o trap.o skas/
endif

obj-$(CONFIG_BLK_DEV_INITRD) += initrd.o
obj-$(CONFIG_GPROF)	+= gprof_syms.o
obj-$(CONFIG_GCOV)	+= gmon_syms.o
obj-$(CONFIG_EARLY_PRINTK) += early_printk.o
obj-$(CONFIG_STACKTRACE) += stacktrace.o
obj-$(CONFIG_MMU) += user_syms.o

USER_OBJS := config.o

include arch/um/scripts/Makefile.rules

targets := config.c config.tmp

# Be careful with the below Sed code - sed is pitfall-rich!
# We use sed to lower build requirements, for "embedded" builders for instance.

$(obj)/config.tmp: $(objtree)/.config FORCE
	$(call if_changed,quote1)

quiet_cmd_quote1 = QUOTE   $@
      cmd_quote1 = sed -e 's/"/\\"/g' -e 's/^/"/' -e 's/$$/\\n",/' \
		   $< > $@

$(obj)/config.c: $(src)/config.c.in $(obj)/config.tmp FORCE
	$(call if_changed,quote2)

quiet_cmd_quote2 = QUOTE   $@
      cmd_quote2 = sed -e '/CONFIG/{'          \
		  -e 's/"CONFIG"//'            \
		  -e 'r $(obj)/config.tmp'     \
		  -e 'a \'                     \
		  -e '""'                      \
		  -e '}'                       \
		  $< > $@
