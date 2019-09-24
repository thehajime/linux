/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_IRQ_H
#define _ASM_LKL_IRQ_H

#ifndef __arch_um__
#define IRQ_STATUS_BITS		(sizeof(long) * 8)
#define NR_IRQS			((int)(IRQ_STATUS_BITS * IRQ_STATUS_BITS))
#endif	/* __arch_um__ */

void run_irqs(void);
void set_irq_pending(int irq);

#include <uapi/asm/irq.h>

#endif
