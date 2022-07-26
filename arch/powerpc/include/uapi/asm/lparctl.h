/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef POWERPC_UAPI_LPARCTL_H
#define POWERPC_UAPI_LPARCTL_H

#include <linux/build_bug.h>
#include <linux/ioctl.h>
#include <linux/types.h>

/* fixme: must reserve in userspace-api/ioctl/ioctl-number.rst */
#define LPARCTL_IOCTL_BASE 0xb2

#define LPARCTL_IO(nr)			_IO(LPARCTL_IOCTL_BASE,nr)
#define LPARCTL_IOR(nr,type)		_IOR(LPARCTL_IOCTL_BASE,nr,type)
#define LPARCTL_IOW(nr,type)		_IOW(LPARCTL_IOCTL_BASE,nr,type)
#define LPARCTL_IOWR(nr,type)		_IOWR(LPARCTL_IOCTL_BASE,nr,type)

/**
 * struct lparctl_get_system_parameter - System parameter retrieval.
 *
 * @token: System parameter token as specified in PAPR+ 7.3.16 "System
 *         Parameters Option".
 * @length: Length of input data, if any, on entry. Length of result on return.
 * @data: Input data, if applicable, on entry. Value of the specified system
 *        parameter on return.
 */
struct lparctl_get_system_parameter {
	__u32 token;
	__u16 pad;
	union {
		/* Result as returned from firmware. */
		__be16 length;
		__u8   data[4002];
	};
};

static_assert(sizeof(struct lparctl_get_system_parameter) == 4008);

#define LPARCTL_GET_SYSPARM LPARCTL_IOWR(0x01, struct lparctl_get_system_parameter)

/* #define LPARCTL_SET_SYSPARM  LPARCTL_IOW(0x02, struct lparctl_getset_system_parameter) */


/* struct lparctl_get_vpd { */
/* 	__u64 userbuf_addr; */
/* }; */

/* #define LPARCTL_GET_VPD     LPARCTL_IOR(0x03, struct lparctl_get_vpd) */

#endif /* POWERPC_UAPI_LPARCTL_H */
