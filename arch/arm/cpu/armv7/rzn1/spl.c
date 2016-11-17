/*
 * common spl init code
 *
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */
#include <asm/io.h>
#include <common.h>
#include <config.h>
#include <spl.h>
#include "renesas/rzn1-sysctrl.h"
#include "renesas/rzn1-utils.h"

u32 spl_boot_device(void)
{
	u32 opmode = (sysctrl_readl(RZN1_SYSCTRL_REG_OPMODE) >> 2) & 3;
	switch (opmode) {
#if defined(CONFIG_SPL_SPI_LOAD)
	case 0:
		return BOOT_DEVICE_SPI;
#endif
#if defined(CONFIG_SPL_NAND_LOAD)
	case 1:
		return BOOT_DEVICE_NAND;
#endif
	}
	/* fallback to hard coded */
#if defined(CONFIG_SPL_SPI_LOAD)
	return BOOT_DEVICE_SPI;
#else
	return BOOT_DEVICE_NAND;
#endif
}

#ifdef CONFIG_SPL_OS_BOOT
int spl_start_uboot(void)
{
	mdelay(50);
	/* break into full u-boot on 'u' */
	if (serial_tstc() && serial_getc() == 'u')
		return 1;
	return 0;
}
#endif
