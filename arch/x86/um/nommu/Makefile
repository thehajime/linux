# SPDX-License-Identifier: GPL-2.0
ifeq ($(CONFIG_X86_32),y)
	BITS := 32
else
	BITS := 64
endif

obj-y = do_syscall_$(BITS).o entry_$(BITS).o syscalls_$(BITS).o os-Linux/
obj-y += zpoline.o

# used by zpoline.c to translate syscall/sysenter instructions
# note: only in x86_64 w/ !CONFIG_MMU
inat_tables_script = $(srctree)/arch/x86/tools/gen-insn-attr-x86.awk
inat_tables_maps = $(srctree)/arch/x86/lib/x86-opcode-map.txt
quiet_cmd_inat_tables = GEN     $@
      cmd_inat_tables = $(AWK) -f $(inat_tables_script) $(inat_tables_maps) > $@
$(obj)/inat-tables.c: $(inat_tables_script) $(inat_tables_maps)
	$(call cmd,inat_tables)

targets += inat-tables.c
$(obj)/../../lib/inat.o: $(obj)/inat-tables.c
obj-y += ../../lib/insn.o ../../lib/inat.o
