/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LKL_ATOMIC_H
#define __LKL_ATOMIC_H

#include <asm-generic/atomic.h>

#ifndef CONFIG_GENERIC_ATOMIC64
#include "atomic64.h"
#endif /* CONFIG_GENERIC_ATOMIC64 */

#endif
