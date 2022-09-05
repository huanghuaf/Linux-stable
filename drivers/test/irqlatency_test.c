/*
 * Allwinner SoCs pinctrl driver.
 *
 * Copyright (C) 2013 Shaorui Huang
 *
 * Shaorui Huang<huangshr@allwinnertech.com>
 * 2013-06-10  add sunxi pinctrl testing case.
 *
 * WimHuang<huangwei@allwinnertech.com>
 * 2015-07-20  transplant it from linux-3.4 to linux-3.10.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ctype.h>

struct irqlatency_data {
	struct gpio_desc *gpio_irq;
	struct gpio_desc *gpio;
	struct completion done;
	spinlock_t lock;
	int irq;
	ktime_t t1;
	ktime_t t2;
};

static struct irqlatency_data *pdata;

static ssize_t store_test(struct class *class, struct class_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long flags;
	spin_lock_irqsave(&pdata->lock, flags);
	pdata->t2 = 0;
	pdata->t1 = ktime_get();
	gpiod_set_value(pdata->gpio, 1);        //set to high for triggle irq
	spin_unlock_irqrestore(&pdata->lock, flags);
	wait_for_completion(&pdata->done);

	if (pdata->t2 <= 0)
		printk("irq get time error\n");

	printk("%s, take time:%lldus", __func__, ktime_us_delta(pdata->t2, pdata->t1));
	return count;
}

static struct class_attribute irqlatency_test_class_attrs =
	__ATTR(test, S_IRUGO | S_IWUSR, NULL, store_test);

static struct attribute *irqlatency_test_attrs[] = {
	&irqlatency_test_class_attrs.attr,
	NULL,
};
ATTRIBUTE_GROUPS(irqlatency_test);

static struct class irqlatency_test_class = {
	.name	= "irqlatency_test",
	.owner	= THIS_MODULE,
	.class_groups = irqlatency_test_groups,
};

irqreturn_t test_irq_handler(int irq, void *data)
{
	struct irqlatency_data *pdata = (struct irqlatency_data *)data;
	spin_lock(&pdata->lock);
	pdata->t2 = ktime_get();
	spin_unlock(&pdata->lock);
	complete(&pdata->done);
	gpiod_set_value(pdata->gpio, 0);	//set to low
	return IRQ_HANDLED;
}


static int irqlatency_test_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->gpio_irq = devm_gpiod_get(dev, "irq", GPIOD_IN);
	if (IS_ERR_OR_NULL(pdata->gpio_irq)) {
		printk("get irq gpio failed\n");
		return PTR_ERR(pdata->gpio_irq);
	}

	pdata->gpio = devm_gpiod_get(dev, "normal", GPIOD_OUT_LOW);;
	if (IS_ERR_OR_NULL(pdata->gpio)) {
		printk("get normal test gpios failed\n");
		return PTR_ERR(pdata->gpio);
	}
	gpiod_set_value(pdata->gpio, 0);	//set to low

	pdata->irq = gpiod_to_irq(pdata->gpio_irq);
	if (pdata->irq < 0) {
		printk("gpio to irq failed\n");
		return pdata->irq;
	}

	ret = request_irq(pdata->irq, test_irq_handler, IRQF_TRIGGER_RISING, "irq_latency_test", pdata);
	if (ret < 0) {
		dev_err(dev, "request_irq failed with error %d\n", ret);
		return ret;
	}

	spin_lock_init(&pdata->lock);

	ret = class_register(&irqlatency_test_class);
	if (ret < 0) {
		pr_err("register test class failed: %d\n", ret);
		return ret;
	}
	init_completion(&pdata->done);
	pdev->dev.class = &irqlatency_test_class;
	dev_set_name(&pdev->dev, "Vdevice");
	platform_set_drvdata(pdev, pdata);

	printk("%s probe finish\n", __func__);
	return 0;
}

static int irqlatency_test_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id irqlatency_test_match[] = {
	{ .compatible = "irqlatency-test"},
	{}
};

static struct platform_driver irqlatency_test_driver = {
	.probe = irqlatency_test_probe,
	.remove	= irqlatency_test_remove,
	.driver = {
		.name = "Vdevice",
		.owner = THIS_MODULE,
		.of_match_table = irqlatency_test_match,
	},
};

static int __init irqlatency_test_init(void)
{
	int ret;
	ret = platform_driver_register(&irqlatency_test_driver);
	if (ret) {
		pr_warn("register irq latency test  platform driver failed\n");
		return -EINVAL;
	}

	return 0;
}

static void __exit irqlatency_test_exit(void)
{
	platform_driver_unregister(&irqlatency_test_driver);
}

module_init(irqlatency_test_init);
module_exit(irqlatency_test_exit);
MODULE_AUTHOR("Huafeng.huang<huafeng.huang@semidrive.com");
MODULE_DESCRIPTION("Semidrive irq latency test driver");
MODULE_LICENSE("GPL");
