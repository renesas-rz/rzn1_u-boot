/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	BSD-2-Clause
 */
#include <asm/io.h>
#include <common.h>
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-utils.h"
#include "renesas/pinctrl-rzn1.h"

#define GPIO_LVL1_CONFIG_B(x)		(RZN1_PINCTRL_BASE + (x << 2))
#define GPIO_LVL2_CONFIG(x)		(RZN1_PINCTRL_L2_BASE + (x << 2))
#define GPIO_LVL1_STATUS_PROTECT	(RZN1_PINCTRL_BASE + 0x400)
#define GPIO_LVL2_STATUS_PROTECT	(RZN1_PINCTRL_L2_BASE + 0x400)
#define WRITE_ACCESS_MASK		0xfffffff8
#define DISABLE_WRITE_ACCESS		1
#define PADS_FUNCTION_USE_L2_SEL	15
#define RZN1_L1_PADFUNC_MASK		0xf
#define RZN1_L2_PADFUNC_MASK		0x3f

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

	/* Set pad function */
	reg &= ~RZN1_L1_PADFUNC_MASK;
	reg |= func & RZN1_L1_PADFUNC_MASK;

	if (attrib & RZN1_DRIVE_SET) {
		reg &= ~RZN1_DRIVE_MASK;
		reg |= attrib & RZN1_DRIVE_MASK;
	}

	if (attrib & RZN1_PULL_SET) {
		reg &= ~RZN1_PULL_MASK;
		reg |= attrib & RZN1_PULL_MASK;
	}

	pinmux_unprotect(GPIO_LVL1_STATUS_PROTECT);
	writel(reg, GPIO_LVL1_CONFIG_B(pin));
	pinmux_protect(GPIO_LVL1_STATUS_PROTECT);
}

static void rzn1_pinmux_mdio_select(u8 mdio, u32 func)
{
       pinmux_unprotect(GPIO_LVL2_STATUS_PROTECT);
       writel(func, GPIO_LVL2_STATUS_PROTECT + 4 + (mdio * 4));
       pinmux_protect(GPIO_LVL2_STATUS_PROTECT);
}

void rzn1_pinmux_select(u8 pin, u32 func, u32 attrib)
{
	/* Special 'virtual' pins for the MDIO muxing */
	if (pin >= RZN1_MDIO_BUS0 && pin <= RZN1_MDIO_BUS1) {
		if (func >= RZN1_FUNC_MDIO_MUX_HIGHZ &&
				func <= RZN1_FUNC_MDIO_MUX_SWITCH) {
			pin -= RZN1_MDIO_BUS0;
			func -= RZN1_FUNC_MDIO_MUX_HIGHZ;
			rzn1_pinmux_mdio_select(pin, func);
		}
		return;
	}

	if (func >= RZN1_FUNC_LEVEL2_OFFSET) {
		func -= RZN1_FUNC_LEVEL2_OFFSET;

		/* Level 1 first otherwise you can introduce a glitch */
		set_lvl1_pinmux(pin, PADS_FUNCTION_USE_L2_SEL, attrib);

		/* Level 2 */
		pinmux_unprotect(GPIO_LVL2_STATUS_PROTECT);
		writel(func, GPIO_LVL2_CONFIG(pin));
		pinmux_protect(GPIO_LVL2_STATUS_PROTECT);
	} else {
		/* Level 1 */
		set_lvl1_pinmux(pin, func, attrib);
	}
}

void rzn1_pinmux_set(u32 setting)
{
	u8 pin = setting & 0xff;
	u32 func = (setting >> RZN1_MUX_FUNC_BIT) & 0x7f;
	u32 attrib = 0;
	u32 tmp;

	if (setting & (1 << RZN1_MUX_HAS_DRIVE_BIT)) {
		tmp = (setting >> RZN1_MUX_DRIVE_BIT) & 3;
		attrib |= RZN1_DRIVE_SET | (tmp << 10);
	}

	if (setting & (1 << RZN1_MUX_HAS_PULL_BIT)) {
		tmp = (setting >> RZN1_MUX_PULL_BIT) & 3;
		attrib |= RZN1_PULL_SET | (tmp << 8);
	}

	rzn1_pinmux_select(pin, func, attrib);
}
