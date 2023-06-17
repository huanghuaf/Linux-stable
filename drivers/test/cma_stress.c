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

#define pr_fmt(fmt) "cma_stress: " fmt

#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <asm/uaccess.h>
#include <linux/cma_stress.h>

struct cma_test_devices {
	struct miscdevice misc;
};

static int cma_stress_test_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int cma_stress_test_release(struct inode *inode, struct file *file)
{
	return 0;
}

static long cma_stess_test_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	struct miscdevice *miscdev = filp->private_data;
	struct cma_region data;
	u64 length = 0;

	if (_IOC_SIZE(cmd) > sizeof(data))
		return -EINVAL;

	if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
		return -EFAULT;

	length = data.length;

	switch (cmd) {
	case CMA_STRESS_TEST_ALLOC:
		memset(&data, 0, sizeof(data));
		data.virt_addr = dma_alloc_coherent(miscdev->parent,
						    length, &data.phy_addr, GFP_KERNEL);
		if (IS_ERR_OR_NULL(data.virt_addr))
			return -ENOMEM;
		data.length = length;
		if (copy_to_user((void __user *)arg, &data, sizeof(data)))
			return -EFAULT;
		break;
	case CMA_STRESS_TEST_FREE:
		if (IS_ERR_OR_NULL(data.virt_addr) || length == 0)
			return -EINVAL;

		dma_free_coherent(miscdev->parent, length, data.virt_addr,
				  data.phy_addr);
		break;
	}

	return 0;
}

static const struct file_operations cma_stress_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = cma_stess_test_ioctl,
	.compat_ioctl = cma_stess_test_ioctl,
	.open = cma_stress_test_open,
	.release = cma_stress_test_release,
};

static int __init cma_stress_test_probe(struct platform_device *pdev)
{
	struct cma_test_devices *testdev;
	int ret;

	testdev = devm_kzalloc(&pdev->dev, sizeof(struct cma_test_devices),
			       GFP_KERNEL);
	if (!testdev)
		return -ENOMEM;

	testdev->misc.minor = MISC_DYNAMIC_MINOR;
	testdev->misc.name = "cma_stess_dev";
	testdev->misc.fops = &cma_stress_fops;
	testdev->misc.parent = &pdev->dev;
	ret = misc_register(&testdev->misc);
	if (ret) {
		pr_err("failed to register misc device.\n");
		return ret;
	}

	/* FIXME: use dma_set_mask_and_coherent() and check result */
	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	//ret = dma_set_mask_and_coherent(testdev->misc.parent,
	//				DMA_BIT_MASK(32));
	if (ret) {
		pr_err("set dma and dma coherent mask fail, ret:%d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, testdev);
	return 0;
}

static int cma_stress_test_remove(struct platform_device *pdev)
{
	struct cma_test_devices *testdev;

	testdev = platform_get_drvdata(pdev);
	if (!testdev)
		return -ENODATA;

	misc_deregister(&testdev->misc);
	return 0;
}

static struct platform_device *cma_stress_test_pdev;
static struct platform_driver cma_stress_test_platform_driver = {
	.remove = cma_stress_test_remove,
	.driver = {
		.name = "cma-stress-test",
	},
};

static int __init cma_stress_test_init(void)
{
	cma_stress_test_pdev = platform_device_register_simple("cma-stress-test",
							-1, NULL, 0);
	if (IS_ERR(cma_stress_test_pdev))
		return PTR_ERR(cma_stress_test_pdev);

	return platform_driver_probe(&cma_stress_test_platform_driver,
				     cma_stress_test_probe);
}

static void __exit cma_stress_test_exit(void)
{
	platform_driver_unregister(&cma_stress_test_platform_driver);
	platform_device_unregister(cma_stress_test_pdev);
}
module_init(cma_stress_test_init);
module_exit(cma_stress_test_exit);
MODULE_LICENSE("GPL v2");
