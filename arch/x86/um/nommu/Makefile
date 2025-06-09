# SPDX-License-Identifier: GPL-2.0
ifeq ($(CONFIG_X86_32),y)
	BITS := 32
else
	BITS := 64
endif

obj-y = do_syscall_$(BITS).o entry_$(BITS).o os-Linux/
