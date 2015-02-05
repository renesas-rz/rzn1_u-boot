/*
 * platform_device.h - generic, centralized driver model
 *
 * This header file defines some structs that most Linux drivers use.
 * It's not intended to replicate the functionality, it's just enough
 * to make most of the code in a Linux driver work on U-Boot.
 *
 */

#ifndef _LINUX_DRIVER_H_
#define  _LINUX_DRIVER_H_

struct device {
	void *platform_data;
};

struct platform_device {
	struct device dev;
};

#endif /* _LINUX_DRIVER_H_ */
