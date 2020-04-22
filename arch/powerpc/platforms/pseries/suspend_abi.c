// SPDX-License-Identifier: GPL-2.0-or-later
#include <asm/firmware.h>
#include <asm/machdep.h>
#include <linux/capability.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include "vasi_suspend_session.h"
#include "suspend_internal.h"

static struct device suspend_dev;

static struct bus_type suspend_subsys = {
	.name = "power",
	.dev_name = "power",
};

/**
 * store_hibernate - Initiate partition hibernation
 * @dev:		subsys root device
 * @attr:		device attribute struct
 * @buf:		buffer
 * @count:		buffer size
 *
 * Write the stream ID received from the HMC to this file
 * to trigger hibernating the partition
 *
 * Return value:
 * 	number of bytes printed to buffer / other on failure
 **/
static ssize_t store_hibernate(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct papr_lpar_suspend_session session;
	unsigned long handle;
	ssize_t ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = kstrtoul(buf, 16, &handle);
	if (ret)
		goto done;

	vasi_suspend_session_init(&session, handle, pseries_suspend_default_ops());

	ret = vasi_suspend_session_run(&session);
	if (ret)
		goto done;

	ret = count;
done:
	return ret;
}

#define USER_DT_UPDATE	0
#define KERN_DT_UPDATE	1

/**
 * show_hibernate - Report device tree update responsibilty
 * @dev:		subsys root device
 * @attr:		device attribute struct
 * @buf:		buffer
 *
 * Report whether a device tree update is performed by the kernel after a
 * resume, or if drmgr must coordinate the update from user space.
 *
 * Return value:
 *	0 if drmgr is to initiate update, and 1 otherwise
 **/
static ssize_t show_hibernate(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%d\n", KERN_DT_UPDATE);
}

static DEVICE_ATTR(hibernate, 0644, show_hibernate, store_hibernate);

/**
 * pseries_suspend_sysfs_register - Register with sysfs
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int pseries_suspend_sysfs_register(struct device *dev)
{
	int rc;

	if ((rc = subsys_system_register(&suspend_subsys, NULL)))
		return rc;

	dev->id = 0;
	dev->bus = &suspend_subsys;

	if ((rc = device_create_file(suspend_subsys.dev_root, &dev_attr_hibernate)))
		goto subsys_unregister;

	return 0;

subsys_unregister:
	bus_unregister(&suspend_subsys);
	return rc;
}

static int __init pseries_suspend_abi_init(void)
{
	int ret;

	ret = -ENODEV;
	if (!firmware_has_feature(FW_FEATURE_LPAR))
		goto done;

	ret = pseries_suspend_sysfs_register(&suspend_dev);
done:
	return ret;
}
machine_device_initcall(pseries, pseries_suspend_abi_init);
