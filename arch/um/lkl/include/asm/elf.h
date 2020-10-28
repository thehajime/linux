/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_LIBMODE_ELF_H
#define __UM_LIBMODE_ELF_H

/* no proper elf loader support yet */
#define elf_check_arch(x) 0

#ifdef CONFIG_64BIT
#define ELF_CLASS ELFCLASS64
#else
#define ELF_CLASS ELFCLASS32
#endif

/*
 * ELF register definitions.
 */
#define elf_gregset_t long
#define elf_fpregset_t double
#endif
