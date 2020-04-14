// SPDX-License-Identifier: GPL-2.0-or-later
/*
  * Copyright (C) 2010 Brian King IBM Corporation
  */

#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/suspend.h>
#include <linux/stat.h>
#include <asm/firmware.h>
#include <asm/hvcall.h>
#include <asm/machdep.h>
#include <asm/mmu.h>
#include <asm/rtas.h>
#include <asm/topology.h>
#include "papr-suspend.h"
#include "../../kernel/cacheinfo.h"

static struct device suspend_dev;

/**
 * pseries_suspend_enter - Final phase of hibernation
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int pseries_suspend_enter(suspend_state_t state)
{
	int fwrc;
	int ret;

	fwrc = rtas_call(rtas_token("ibm,suspend-me"), 0, 1, NULL);
	if (fwrc)
		pr_err("ibm,suspend-me failed with %i\n", fwrc);
	switch (fwrc) {
	case 0:
		ret = 0;
		break;
	case RTAS_SUSPEND_ABORTED:
		ret = -ECANCELED;
		break;
	case RTAS_NOT_SUSPENDABLE:
	case RTAS_THREADS_ACTIVE:
	case RTAS_OUTSTANDING_COPROC:
		ret = -EBUSY;
		break;
	case -1: /* hw error */
	default:
		ret = -EIO;
	}

	return ret;
}

static void pseries_suspend_wake(void)
{
	post_mobility_fixup();
}

static int poll_vasi_state(struct papr_lpar_suspend_session *session,
			   vasi_suspend_state_t *state)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long hvrc;
	int ret;

	ret = 0;
	hvrc = plpar_hcall(H_VASI_STATE, retbuf, session->handle);

	switch (hvrc) {
	case H_FUNCTION:
		/*
		 * Allow suspend to proceed on hypervisors that don't
		 * have H_VASI_STATE. If the subsequent ibm,suspend-me
		 * fails we'll recover then.
		 */
		pr_notice_once("H_VASI_STATE not available, fabricating "
			       "'Suspending' response.\n");
		*state = VASI_SUSPEND_STATE_SUSPENDING;
		break;
	case H_SUCCESS:
		*state = retbuf[0];
		break;
	default:
		pr_err_ratelimited("H_VASI_STATE(handle=0x%llx) failed (%ld)\n",
				   session->handle, hvrc);
		ret = -EIO;
		break;
	}

	return ret;
}

static int do_suspend(struct papr_lpar_suspend_session *session)
{
	return pm_suspend(PM_SUSPEND_MEM);
}

static int cancel_suspend(struct papr_lpar_suspend_session *session)
{
	s64 signal;
	s64 reason;
	u64 handle;
	long hvrc;
	int ret;

	handle = session->handle;
	signal = H_VASI_SIGNAL_CANCEL;
	reason = 0;

	hvrc = plpar_hcall_norets(H_VASI_SIGNAL, handle, signal, reason);

	switch(hvrc) {
	case H_FUNCTION:
		/*
		 * If the hypervisor doesn't implement this facility,
		 * note it but proceed; it makes no difference to the
		 * OS.
		 */
		pr_notice_once("Attempted to cancel suspension, but "
			       "H_VASI_SIGNAL not available.\n");
		fallthrough;
	case H_SUCCESS:
		ret = 0;
		break;
	default:
		ret = -EIO;
		pr_err_ratelimited("H_VASI_SIGNAL(handle=0x%llx, signal=%lld) "
				   "failed (%ld)\n", handle, signal, hvrc);
		break;
	}

	return ret;
}

static const struct papr_suspend_ops lpar_hibernate_ops = {
	.poll_vasi_state = poll_vasi_state,
	.do_suspend = do_suspend,
	.cancel_suspend = cancel_suspend,
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

	papr_suspend_session_init(&session, handle, &lpar_hibernate_ops);

	ret = papr_suspend_lpar(&session);
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

static struct bus_type suspend_subsys = {
	.name = "power",
	.dev_name = "power",
};

static const struct platform_suspend_ops pseries_suspend_ops = {
	.valid		= suspend_valid_only_mem,
	.enter		= pseries_suspend_enter,
	.wake		= pseries_suspend_wake,
};

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

/**
 * pseries_suspend_init - initcall for pSeries suspend
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int __init pseries_suspend_init(void)
{
	int rc;

	if (!firmware_has_feature(FW_FEATURE_LPAR))
		return 0;

	if ((rc = pseries_suspend_sysfs_register(&suspend_dev)))
		return rc;

	suspend_set_ops(&pseries_suspend_ops);
	return 0;
}
machine_device_initcall(pseries, pseries_suspend_init);
