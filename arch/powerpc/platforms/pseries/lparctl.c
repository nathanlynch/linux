// SPDX-License-Identifier: GPL-2.0-only
/*
 * Character device for accessing pseries/PAPR platform-specific
 * facilities.
 */
#define pr_fmt(fmt) "lparctl: " fmt

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <asm/lparctl.h>
#include <asm/machdep.h>
#include <asm/rtas.h>

static long lparctl_get_sysparm(unsigned long arg)
{
	struct lparctl_getset_system_parameter *udesc = u64_to_user_ptr((u64)arg);
	struct lparctl_getset_system_parameter kdesc;
	long ret;
	int fwrc;

	if (copy_from_user(&kdesc, udesc, sizeof(kdesc)))
		return -EFAULT;

	pr_info("arg = 0x%llx\n", (unsigned long long)arg);
	pr_info("arg->(addr=0x%llx, size=0x%x, param=%d)\n",
		kdesc.userbuf_addr, kdesc.userbuf_size, kdesc.parameter);


	spin_lock(&rtas_data_buf_lock);
	memset(rtas_data_buf, 0, RTAS_DATA_BUF_SIZE);

	/* fixme: copy_*_user under spinlock - bad */
	if (copy_from_user(rtas_data_buf, u64_to_user_ptr(kdesc.userbuf_addr),
			   min(4002UL, (unsigned long)kdesc.userbuf_size))) {
		ret = -EFAULT;
		goto unlock;
	}
	fwrc = rtas_call(rtas_token("ibm,get-system-parameter"), 3, 1, NULL,
			 kdesc.parameter, __pa(rtas_data_buf), RTAS_DATA_BUF_SIZE);
	if (fwrc != 0) {
		/* todo: map return value to errno */
		pr_err("sysparm token %d got RTAS rc %d\n", kdesc.parameter, fwrc);
		ret = -EIO;
		goto unlock;
	}
	if (copy_to_user(u64_to_user_ptr(kdesc.userbuf_addr), rtas_data_buf,
			 min(4002UL, (unsigned long)kdesc.userbuf_size))) {
		ret = -EFAULT;
		goto unlock;
	}
	ret = 0;
unlock:
	spin_unlock(&rtas_data_buf_lock);
	return ret;
}

static long lparctl_dev_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	long ret = -EINVAL;

	switch(ioctl) {
	case LPARCTL_GET_SYSPARM:
		pr_info("got get-sysparm command\n");
		ret = lparctl_get_sysparm(arg);
		break;
	default:
		break;
	}
	return ret;
}

static struct file_operations lparctl_ops = {
	.unlocked_ioctl = lparctl_dev_ioctl,
	.llseek		= noop_llseek,
};

static struct miscdevice lparctl_dev = {
	MISC_DYNAMIC_MINOR,
	"lparctl",
	&lparctl_ops,
};

static __init int lparctl_init(void)
{
	int ret;

	ret = misc_register(&lparctl_dev);

	return ret;
}
machine_device_initcall(pseries, lparctl_init);
