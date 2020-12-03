/*
 * drivers/staging/android/uapi/dmabuf-test.h
 *
 * Copyright (C) 2020 Huafenghuang@allwinnertech.com, Inc.
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

#ifndef _UAPI_LINUX_DMABUF_TEST_H
#define _UAPI_LINUX_DMABUF_TEST_H

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * struct dmabug_test_rw_data - metadata passed to the kernel to read handle
 * @fd:	a pointer to an area at least as large as size
 */
struct dmabuf_test_rw_data {
	int fd;
};

#define DMABUF_IOC_MAGIC		'D'

/**
 * DOC: ION_IOC_TEST_SET_DMA_BUF - attach a dma buf to the test driver
 *
 * Attaches a dma buf fd to the test driver.  Passing a second fd or -1 will
 * release the first fd.
 */
#define DMABUF_IOC_TEST_GET_FD_FROM_KERNEL \
			_IOR(DMABUF_IOC_MAGIC, 0x0, struct dmabuf_test_rw_data)

#define DMABUF_IOC_TEST_GET_FD_FROM_USER \
			_IOW(DMABUF_IOC_MAGIC, 0x1, struct dmabuf_test_rw_data)
#endif /* _UAPI_LINUX_DMABUF_TEST_H */
