// SPDX-License-Identifier: GPL-2.0-only
/*
 * Character device for accessing pseries/PAPR platform-specific
 * facilities.
 */
#define pr_fmt(fmt) "lparctl: " fmt

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/lparctl.h>
#include <asm/machdep.h>
#include <asm/rtas.h>

static long lparctl_get_sysparm(unsigned long arg)
{
	struct lparctl_get_system_parameter *gsp;
	long ret;
	int fwrc;

	/*
	 * Special case to allow user space to probe the command.
	 */
	if (arg == 0)
		return 0;

	gsp = memdup_user(u64_to_user_ptr((u64)arg), sizeof(*gsp));
	if (IS_ERR(gsp)) {
		ret = PTR_ERR(gsp);
		goto err_return;
	}

	ret = -EINVAL;
	if (gsp->pad != 0)
		goto err_free;

	do {
		static_assert(sizeof(gsp->data) <= sizeof(rtas_data_buf));

		spin_lock(&rtas_data_buf_lock);
		memcpy(rtas_data_buf, gsp->data, sizeof(gsp->data));
		fwrc = rtas_call(rtas_token("ibm,get-system-parameter"), 3, 1,
				 NULL, gsp->token, __pa(rtas_data_buf));
		memcpy(gsp->data, rtas_data_buf, sizeof(gsp->data));
		spin_unlock(&rtas_data_buf_lock);
	} while (rtas_busy_delay(fwrc));

	switch (fwrc) {
	case 0:
		ret = 0;
		break;
	case -3:
		/*
		 * Parameter not supported/implemented on this system.
		 */
		ret = -EOPNOTSUPP;
		break;
	case -9002:
		/*
		 * This partition is not authorized to retrieve the given
		 * parameter.
		 */
		ret = -EPERM;
		break;
	case -9999:
		/*
		 * Parameter error. Unclear why this would happen, but it's
		 * in the spec.
		 */
		ret = -EINVAL;
		break;
	case -1:
	default:
		ret = -EIO;
		break;
	}
err_free:
	kfree(gsp);
err_return:
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
		return -ENOIOCTLCMD;
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
