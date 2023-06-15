// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, Semidriver Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/time64.h>
#include <linux/printk.h>
//#include <linux/sched/clock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#ifdef CONFIG_IRQ_STORM_DEFAULT_ON
unsigned int __read_mostly irq_storm_detect_enable = 1;
#else
unsigned int __read_mostly irq_storm_detect_enable;
#endif

#ifdef CONFIG_PANIC_ON_IRQ_STORM
static unsigned int __read_mostly irq_storm_panic = 1;
#else
static unsigned int __read_mostly irq_storm_panic;
#endif

#ifdef CONFIG_IRQ_STORM_DISABLE_IRQ
static unsigned int __read_mostly irq_storm_disable_irq = 1;
#else
static unsigned int __read_mostly irq_storm_disable_irq;
#endif

void irq_storm_check(struct irq_desc *desc)
{
	u64 current_time = 0;
	s64 delta = 0;
	unsigned int irq = irq_desc_get_irq(desc);


	current_time = sched_clock();
	desc->irq_storm_count++;

	if (desc->start_time == 0) {
		desc->start_time = current_time;
		return;
	}

	if (desc->irq_storm_count >= desc->irq_storm_throttled) {
		WARN_ONCE(true,
			  "irq storm detect, irq:%d, irq storm count:%d\n",
			  irq, desc->irq_storm_count);

		if (irq_storm_disable_irq)
			disable_irq_nosync(irq);

		if (irq_storm_panic)
			panic("irq storm");
	}

	delta = current_time - desc->start_time;

	/* update the start time for next */
	if (delta >= 100 * NSEC_PER_MSEC) {
		desc->start_time = current_time;
		desc->irq_storm_count = 0;
	}

}


static int __init irq_storm_detect_setup(char *str)
{
	int rc = kstrtouint(str, 0, &irq_storm_detect_enable);

	if (rc)
		return rc;

	return 1;
}

__setup("irqstorm_detect=", irq_storm_detect_setup);
module_param(irq_storm_detect_enable, int, 0644);
MODULE_PARM_DESC(irq_storm_detect_enable,
		 "Enable irq storm detection when true");

static int __init irq_storm_panic_setup(char *str)
{
	int rc = kstrtouint(str, 0, &irq_storm_panic);

	if (rc)
		return rc;

	return 1;
}
__setup("irqstorm_panic=", irq_storm_panic_setup);
module_param(irq_storm_panic, int, 0644);
MODULE_PARM_DESC(irq_storm_panic, "Do panic when irq storm detection");

static int __init irq_storm_disable_irq_setup(char *str)
{
	int rc = kstrtouint(str, 0, &irq_storm_disable_irq);

	if (rc)
		return rc;

	return 1;
}
__setup("irqstorm_disable_irq=", irq_storm_disable_irq_setup);
module_param(irq_storm_disable_irq, int, 0644);
MODULE_PARM_DESC(irq_storm_disable_irq, "Disable irq when irq storm detection");
