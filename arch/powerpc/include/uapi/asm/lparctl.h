/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef POWERPC_UAPI_LPARCTL_H
#define POWERPC_UAPI_LPARCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* fixme: must reserve in userspace-api/ioctl/ioctl-number.rst */
#define LPARCTL_IOCTL_BASE 0xb2

#define LPARCTL_IO(nr)			_IO(LPARCTL_IOCTL_BASE,nr)
#define LPARCTL_IOR(nr,type)		_IOR(LPARCTL_IOCTL_BASE,nr,type)
#define LPARCTL_IOW(nr,type)		_IOW(LPARCTL_IOCTL_BASE,nr,type)
#define LPARCTL_IOWR(nr,type)		_IOWR(LPARCTL_IOCTL_BASE,nr,type)

/**
 * struct lparctl_getset_system_parameter - System parameter control block.
 * @userbuf_addr: User buffer address.
 *                For LPARCTL_GET_SYSPARM: May contain input data on entry,
 *                depending on the parameter. On return, contains the requested
 *                parameter value.
 *                For LPARCTL_SET_SYSPARM: On entry, contains the desired new
 *                value for the parameter. On return, value is unchanged.
 * @userbuf_size: Size of I/O buffer.
 * @parameter:    System parameter token as specified in PAPR+ 7.3.16 "System
 *                Parameters Option".
 */
struct lparctl_getset_system_parameter {
	__u64 userbuf_addr;
	__u32 userbuf_size;
	__u16 parameter;
	__u16 reserved;
};

#define LPARCTL_GET_SYSPARM LPARCTL_IOWR(0x01, struct lparctl_getset_system_parameter)

#endif /* POWERPC_UAPI_LPARCTL_H */
