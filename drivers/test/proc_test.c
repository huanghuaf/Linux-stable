#include <linux/sysrq.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/moduleparam.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

#define MAX_LATENCY_TIME	(100 * 1000)	/* us */

struct proc_test_key_op {
	void (*handler)(int);
	char *help_msg;
	char *action_msg;
};

static DEFINE_SPINLOCK(proc_test_lock);

static void proc_test_handle_loglevel(int key)
{
	int i;

	i = key - '0';
	console_loglevel = CONSOLE_LOGLEVEL_DEFAULT;
	pr_info("Loglevel set to %d\n", i);
	console_loglevel = i;
}

static void proc_test_handle_irq_latency_test(int key)
{
	unsigned long flags;
	unsigned long delt = 0;
	struct timeval tv1, tv2, tv3;

	do_gettimeofday(&tv1);
	spin_lock_irqsave(&proc_test_lock, flags);

	/*try to close irq for a long time*/
	while (delt < MAX_LATENCY_TIME) {
		do_gettimeofday(&tv2);
		delt = (tv2.tv_sec - tv1.tv_sec) * 1000000 + tv2.tv_usec - tv1.tv_usec;		//us
	}
	spin_unlock_irqrestore(&proc_test_lock, flags);

	do_gettimeofday(&tv3);

	delt = (tv3.tv_sec - tv1.tv_sec) * 1000000 + tv3.tv_usec - tv1.tv_usec;
	printk("take time %ld us\n", delt);


}

static struct proc_test_key_op proc_test_loglevel_op = {
	.handler	= proc_test_handle_loglevel,
	.help_msg	= "loglevel(0-9)",
	.action_msg	= "Changing Loglevel",
};

static void proc_test_softlock_func(int key)
{
	spin_lock(&proc_test_lock);
	while(1)
		msleep(100);
	spin_unlock(&proc_test_lock);
}

static void proc_test_hardlock_func(int key)
{
	unsigned long flags;

	spin_lock_irqsave(&proc_test_lock, flags);
	while(1)
		msleep(100);
	spin_unlock_irqrestore(&proc_test_lock, flags);
}

static struct proc_test_key_op proc_test_irq_latency_op = {
	.handler	= proc_test_handle_irq_latency_test,
	.help_msg	= "irq_latency_test",
	.action_msg	= "test ftrace debug irq latency",
};

static struct proc_test_key_op proc_test_softlock_op = {
	.handler	= proc_test_softlock_func,
	.help_msg	= "softlock test",
	.action_msg	= "test softlock in kernel"
};

static struct proc_test_key_op proc_test_hardlock_op = {
	.handler	= proc_test_hardlock_func,
	.help_msg	= "hardlock test",
	.action_msg	= "test hardlock in kernel"
};

static struct proc_test_key_op *proc_test_key_table[36] = {
	&proc_test_loglevel_op,		/* 0 */
	&proc_test_loglevel_op,		/* 1 */
	&proc_test_loglevel_op,		/* 2 */
	&proc_test_loglevel_op,		/* 3 */
	&proc_test_loglevel_op,		/* 4 */
	&proc_test_loglevel_op,		/* 5 */
	&proc_test_loglevel_op,		/* 6 */
	&proc_test_loglevel_op,		/* 7 */
	&proc_test_loglevel_op,		/* 8 */
	&proc_test_loglevel_op,		/* 9 */
	NULL,				/* a */
	NULL,				/* b */
	NULL,				/* c */
	NULL,				/* d */
	NULL,				/* e */
	NULL,				/* f */
	NULL,				/* g */
	&proc_test_hardlock_op,		/* h */
	&proc_test_irq_latency_op,	/* i */
	NULL,				/* j */
	NULL,				/* k */
	NULL,				/* l */
	NULL,				/* m */
	NULL,				/* n */
	NULL,				/* o */
	NULL,				/* p */
	NULL,				/* q */
	NULL,				/* r */
	&proc_test_softlock_op,		/* s */
	NULL,				/* t */
	NULL,				/* u */
	NULL,				/* v */
	NULL,				/* w */
	NULL,				/* x */
	NULL,				/* y */
	NULL,				/* z */
};

/* key2index calculation, -1 on invalid index */
static int proc_test_key_table_key2index(int key)
{
	int retval;

	if ((key >= '0') && (key <= '9'))
		retval = key - '0';
	else if ((key >= 'a') && (key <= 'z'))
		retval = key + 10 - 'a';
	else
		retval = -1;
	return retval;
}

/*
 * get and put functions for the table.
 */
static struct proc_test_key_op *proc_test_get_key_op(int key)
{
        struct proc_test_key_op *op_p = NULL;
        int i;

	i = proc_test_key_table_key2index(key);
	if (i != -1)
	        op_p = proc_test_key_table[i];

        return op_p;
}
static void __handle_proc_test(int key)
{
	struct proc_test_key_op *key_op;
	int orig_log_level;
	int i;

	rcu_sysrq_start();
	rcu_read_lock();

	orig_log_level = console_loglevel;
	console_loglevel = CONSOLE_LOGLEVEL_DEFAULT;

	pr_info("proc test :");
	key_op = proc_test_get_key_op(key);

	if (key_op) {
		pr_cont("%s\n", key_op->action_msg);
		console_loglevel = orig_log_level;
		key_op->handler(key);
	} else {
		pr_cont("HELP : ");
		for (i = 0; i < ARRAY_SIZE(proc_test_key_table); i++) {
			if (proc_test_key_table[i]) {
				int j;
				for (j = 0; proc_test_key_table[i] !=
						proc_test_key_table[j]; j++)
					if (j != i)
						continue;
				pr_cont("%s ", proc_test_key_table[i]->help_msg);
			}
		}
		pr_cont("\n");
		console_loglevel = orig_log_level;
	}
	rcu_read_unlock();
	rcu_sysrq_end();
}

static ssize_t write_proc_test(struct file *file, const char __user *buf,
				       size_t count, loff_t *ppos)
{
	if (count) {
		char c;
		if (get_user(c, buf))
			return -EFAULT;
		__handle_proc_test(c);
	}

	return count;
}

static const struct file_operations proc_test_operations = {
	.write		= write_proc_test,
	.llseek		= noop_llseek,
};

static void proc_test_procfs(void)
{
	if (!proc_create("proc_test", S_IWUSR, NULL,
			&proc_test_operations))
		pr_err("Failed to register proc interface\n");
}

static int __init proc_test_init(void)
{
	proc_test_procfs();

	return 0;
}

device_initcall(proc_test_init);
