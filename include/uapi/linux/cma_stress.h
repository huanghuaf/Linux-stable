/*
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __LINUX_CMA_STRESS_TEST_H
#define __LINUX_CMA_STRESS_TEST_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct cma_region {
	void *virt_addr;
	dma_addr_t phy_addr;
	u64 length;
};

#define CMA_STRESS_TEST_IOC_MAGIC		'C'

#define CMA_STRESS_TEST_ALLOC		_IOWR(CMA_STRESS_TEST_IOC_MAGIC, 0,\
					      struct cma_region)

#define CMA_STRESS_TEST_FREE		_IOR(CMA_STRESS_TEST_IOC_MAGIC, 1,\
					      struct cma_region)


#endif
