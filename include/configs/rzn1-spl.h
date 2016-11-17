/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 * Phil Edworthy <phil.edworthy@renesas.com>
 *
 * Renesas RZ/N1 device configuration for SPL builds - you should not change these!
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __RZN1_SPL_H
#define __RZN1_SPL_H

#define CONFIG_SPL_FRAMEWORK
#define CONFIG_SPL_BOARD_INIT

/* This is required to fish the ethernet addresses from the environment */
#define CONFIG_SPL_ENV_SUPPORT

#if defined(CONFIG_SPL_NAND_LOAD)
	#define CONFIG_SPL_NAND_DRIVERS
	#define CONFIG_SPL_NAND_ONFI
#endif

#endif /* RZN1_SPL_H */
