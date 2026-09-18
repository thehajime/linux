/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * vma_internal.h
 *
 * Headers required by vma.c, vma_init.c and vma_exec.c, which can be
 * substituted accordingly when testing VMA functionality.
 */

#ifndef __MM_VMA_INTERNAL_H
#define __MM_VMA_INTERNAL_H

#include "vma_common_internal.h"

#ifdef CONFIG_MMU
#include "vma_mmu_internal.h"
#endif

#endif	/* __MM_VMA_INTERNAL_H */
