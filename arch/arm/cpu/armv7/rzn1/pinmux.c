/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <asm/io.h>
#include <common.h>
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-utils.h"
#include "renesas/pinctrl-rzn1.h"

#define GPIO_LVL1_CONFIG_B(x)		(RZN1_PINCTRL_BASE + (x << 2))
#define GPIO_LVL2_CONFIG(x)		(RZN1_CONFIG_UDL_BASE + (x << 2))
#define GPIO_LVL1_STATUS_PROTECT	(RZN1_PINCTRL_BASE + 0x400)
#define GPIO_LVL2_STATUS_PROTECT	(RZN1_CONFIG_UDL_BASE + 0x400)
#define WRITE_ACCESS_MASK		0xfffffff8
#define DISABLE_WRITE_ACCESS		1
#define PADS_FUNCTION_USE_L2_SEL	15

static void pinmux_unprotect(u32 reg)
{
	/* Enable write access to port multiplex registers */
	/* write the address of the register to the register */
	writel(reg & WRITE_ACCESS_MASK, reg);
}

static void pinmux_protect(u32 reg)
{
	/* Disable write access to port multiplex registers */
	/* write the address of the register to the register */
	u32 val = reg;
	val &= WRITE_ACCESS_MASK;
	val |= DISABLE_WRITE_ACCESS;
	writel(val, reg);
}

static void set_lvl1_pinmux(u8 pin, u32 func, u32 attrib)
{
	u32 reg = readl(GPIO_LVL1_CONFIG_B(pin));

	pinmux_unprotect(GPIO_LVL1_STATUS_PROTECT);
	/* Clear Drive strength, Pullup PullDown and PadsFunction to '0' */
	reg &= 0xfffff0f0;
	reg |= func;
	reg |= attrib;
	writel(reg, GPIO_LVL1_CONFIG_B(pin));
	pinmux_protect(GPIO_LVL1_STATUS_PROTECT);
}

void rzn1_pinmux_select(u8 pin, u32 func, u32 attrib)
{
	if (func >= RZN1_FUNC_LEVEL2_OFFSET) {
		func -= RZN1_FUNC_LEVEL2_OFFSET;

		/* Level 2 */
		pinmux_unprotect(GPIO_LVL2_STATUS_PROTECT);
		writel(func, GPIO_LVL2_CONFIG(pin));
		pinmux_protect(GPIO_LVL2_STATUS_PROTECT);

		/* Level 1 */
		set_lvl1_pinmux(pin, PADS_FUNCTION_USE_L2_SEL, attrib);
	} else {
		/* Level 1 */
		set_lvl1_pinmux(pin, func, attrib);
	}
}
