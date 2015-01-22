/*
 * common spl init code
 *
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */
#include <common.h>
#include <config.h>
#include <spl.h>

u32 spl_boot_device(void)
{
#if defined(CONFIG_SPL_SPI_LOAD)
	return BOOT_DEVICE_SPI;
#else
	return BOOT_DEVICE_NAND;
#endif
}
