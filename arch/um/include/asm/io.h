/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_UM_IO_H
#define _ASM_UM_IO_H

#ifndef CONFIG_HAS_IOMEM
#define ioremap ioremap
static inline void __iomem *ioremap(phys_addr_t offset, size_t size)
{
	return (void __iomem *)(unsigned long)offset;
}
#else
#include <lkl/include/asm/io.h>
#endif

#define iounmap iounmap
static inline void iounmap(void __iomem *addr)
{
}

#include <asm-generic/io.h>

#endif
