/*
 *
 * Copyright (C) 2013 Google, Inc.
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

#define pr_fmt(fmt) "dma-buf-test: " fmt

#include <linux/dma-buf.h>
#include <linux/dma-direction.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <linux/vexpress_ion.h>
#include "ion.h"
#include "../uapi/dmabuf_test.h"

struct dmabuf_test_device {
	struct miscdevice misc;
	struct ion_client *client;
	struct ion_handle *handle;
};

static int dmabuf_alloc_dmabuf(struct dmabuf_test_device *dev)
{
	struct ion_handle *handle = NULL;
	unsigned char *buf = NULL;
	unsigned int flags = ION_FLAG_CACHED | ION_FLAG_CACHED_NEEDS_SYNC;

	handle = ion_alloc(dev->client, PAGE_SIZE, 0, (1 << ION_HEAP_TYPE_SYSTEM), flags);

	if (IS_ERR_OR_NULL(handle)) {
		pr_err("ion alloc handle error\n");
		return -EINVAL;
	}

	dev->handle = handle;
	buf = (unsigned char *)ion_map_kernel(dev->client, handle);
	buf[0] = 'e';
	buf[1] = 'f';
	buf[2] = 'g';
	buf[3] = 'h';
	buf[4] = '\n';
	ion_unmap_kernel(dev->client, handle);
	return 0;
}
/*
static int get_dmabuf_from_fd(int fd)
{

}
*/
static int dmabuf_test_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int dmabuf_test_release(struct inode *inode, struct file *file)
{
	return 0;
}

static unsigned long dmabuf_test_ioctl_dir(unsigned int cmd)
{
	switch(cmd) {
	case DMABUF_IOC_TEST_GET_FD_FROM_KERNEL:
	case DMABUF_IOC_TEST_GET_FD_FROM_USER:
		return _IOC_DIR(cmd);
	default:
		return _IOC_DIR(cmd);
	}
}

static long dmabuf_test_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	struct miscdevice *miscdev = filp->private_data;
	struct dmabuf_test_device *dev = container_of(miscdev, struct dmabuf_test_device, misc);
	unsigned int dir;
	struct dmabuf_test_rw_data data;

	dir = dmabuf_test_ioctl_dir(cmd);

	if (_IOC_SIZE(cmd) > sizeof(data))
		return -EINVAL;

	if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
		return -EFAULT;

	if (!(dir & _IOC_WRITE))
		memset(&data, 0, sizeof(data));

	switch(cmd) {
	case DMABUF_IOC_TEST_GET_FD_FROM_KERNEL:
	{

		/* share fd must keep it in device ioctl, otherwise userspace
		 * mapping file faile
		 */
		data.fd = ion_share_dma_buf_fd(dev->client, dev->handle);
		if (data.fd < 0) {
			pr_err("get dmabuf fd error\n");
			return -EBADF;
		}
		break;
	}
	case DMABUF_IOC_TEST_GET_FD_FROM_USER:
	{
		struct dma_buf *dmabuf;
		unsigned char *buf;
		int ret;

		dmabuf = dma_buf_get(data.fd);
		if (IS_ERR_OR_NULL(dmabuf)) {
			pr_err("get dmabuf from dmabuf fd:%d error\n", data.fd);
			return -EINVAL;
		}
		ret = dma_buf_begin_cpu_access(dmabuf, DMA_BIDIRECTIONAL);
		if (ret < 0) {
			pr_err("begin cpu access error\n");
			dma_buf_put(dmabuf);
			return -EFAULT;
		}

		/* buf size is PAGE_SIZE */
		buf = (unsigned char *)dma_buf_kmap(dmabuf, 0);
		if (IS_ERR_OR_NULL(buf)) {
			pr_err("map dmabuf to kernel space error\n");
			return -EFAULT;
		}
		printk("buf data:%s\n", buf);
		ret = dma_buf_end_cpu_access(dmabuf, DMA_BIDIRECTIONAL);
		if (ret < 0) {
			pr_err("end cpu access error\n");
			dma_buf_kunmap(dmabuf, 0, (void *)buf);
			dma_buf_put(dmabuf);
			return -EFAULT;
		}
		dma_buf_kunmap(dmabuf, 0, (void *)buf);
		dma_buf_put(dmabuf);
		break;
	}
	default:
		return -ENOTTY;
	}

	if (dir & _IOC_READ) {
		if (copy_to_user((void __user *)arg, &data, _IOC_SIZE(cmd))) {
			return -EFAULT;
		}
	}

	return 0;
}

static const struct file_operations dmabuf_test_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = dmabuf_test_ioctl,
	.compat_ioctl = dmabuf_test_ioctl,
	.open = dmabuf_test_open,
	.release = dmabuf_test_release,
};

static int __init dmabuf_test_probe(struct platform_device *pdev)
{
	int ret;
	struct dmabuf_test_device *testdev;

	testdev = devm_kzalloc(&pdev->dev, sizeof(struct dmabuf_test_device),
			       GFP_KERNEL);
	if (!testdev)
		return -ENOMEM;

	testdev->misc.minor = MISC_DYNAMIC_MINOR;
	testdev->misc.name = "dmabuf-test";
	testdev->misc.fops = &dmabuf_test_fops;
	testdev->misc.parent = &pdev->dev;
	ret = misc_register(&testdev->misc);
	if (ret) {
		pr_err("failed to register misc device.\n");
		return ret;
	}

	/*create ion client */
	testdev->client = vexpress_ion_client_create("dmabuf-test-client");
	if (!testdev->client) {
		pr_err("failed to create ion client\n");
		return -EINVAL;
	}

	dmabuf_alloc_dmabuf(testdev);
	platform_set_drvdata(pdev, testdev);

	return 0;
}

static int dmabuf_test_remove(struct platform_device *pdev)
{
	struct dmabuf_test_device *testdev;

	testdev = platform_get_drvdata(pdev);
	if (!testdev)
		return -ENODATA;

	misc_deregister(&testdev->misc);
	return 0;
}

static struct platform_device *dmabuf_test_pdev;
static struct platform_driver dmabuf_test_platform_driver = {
	.remove = dmabuf_test_remove,
	.driver = {
		.name = "dmabuf-test",
	},
};

static int __init dmabuf_test_init(void)
{
	dmabuf_test_pdev = platform_device_register_simple("dmabuf-test",
							-1, NULL, 0);
	if (IS_ERR(dmabuf_test_pdev))
		return PTR_ERR(dmabuf_test_pdev);

	return platform_driver_probe(&dmabuf_test_platform_driver, dmabuf_test_probe);
}

static void __exit dmabuf_test_exit(void)
{
	platform_driver_unregister(&dmabuf_test_platform_driver);
	platform_device_unregister(dmabuf_test_pdev);
}

module_init(dmabuf_test_init);
module_exit(dmabuf_test_exit);
MODULE_LICENSE("GPL v2");
