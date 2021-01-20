// SPDX-License-Identifier: GPL-2.0
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <os.h>

void __ndelay(unsigned long nsecs)
{
	long long start = os_nsecs();

	while (os_nsecs() < start + nsecs)
		;
}

void __udelay(unsigned long usecs)
{
	__ndelay(usecs * NSEC_PER_USEC);
}

void __const_udelay(unsigned long xloops)
{
	__udelay(xloops / 0x10c7ul);
}

void __delay(unsigned long loops)
{
	__ndelay(loops / 5);
}

void calibrate_delay(void)
{
}
