/*
 * Evaluation board (RZ/N1 device) init
 *
 * Copyright (C) 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <common.h>
#include <miiphy.h>
#include <netdev.h>
#include <asm/io.h>
#include <linux/sizes.h>
#include <malloc.h>
#include <pca9698.h>
#include <sdhci.h>
#include <spl.h>
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-sysctrl.h"
#include "renesas/rzn1-utils.h"
#include "renesas/rzn1-clocks.h"
#include "renesas/pinctrl-rzn1.h"
#include "cadence_ddr_ctrl.h"

DECLARE_GLOBAL_DATA_PTR;

int board_early_init_f(void)
{
	return 0;
}

/* Called early during device initialisation */
void rzn1_setup_pinmux(void)
{
	u8 i;

	/* UART pins */
	rzn1_pinmux_select(103, RZN1_FUNC_UART0, 0);
	rzn1_pinmux_select(104, RZN1_FUNC_UART0, 0);

	/* QSPI pins */
	for (i = 74; i <= 79; i++)
		rzn1_pinmux_select(i, RZN1_FUNC_QSPI, 0);

	/* NAND pins */
	for (i = 80; i <= 93; i++)
		rzn1_pinmux_select(i, RZN1_FUNC_CLK_ETH_NAND, 0);
	rzn1_pinmux_select(94, RZN1_FUNC_CLK_ETH_NAND, RZN1_PULL_UP);	/* NAND 0 RDY/BSY */

#if defined(CONFIG_DW_I2C)
	/* I2C pins - assumes W7 and W8 in 3 position */
	rzn1_pinmux_select(39, RZN1_FUNC_I2C, 0);
	rzn1_pinmux_select(40, RZN1_FUNC_I2C, 0);
#endif
}

#ifdef CONFIG_USBF_RENESAS
#include <usb.h>

/**
 * @brief board_usb_init - Configure board specific
 * clocks for the USB blocks.
 *
 * @return 0
 */
int board_usb_init(int index, enum usb_init_type init)
{
	/* Enable USB clocks */
	rzn1_clk_set_gate(RZN1_CLK_USBPM_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_USBF_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_USBH_ID, 1);

	return 0;
}
#endif

int board_init(void)
{
	u32 val;

	/*
	 * Enable USB Function on port 1 as the Linux kernel doesn't deal with
	 * these irregular register bits
	 */
	sysctrl_writel((1 << RZN1_SYSCTRL_REG_CFG_USB_FRCLK48MOD), RZN1_SYSCTRL_REG_CFG_USB);
	/* Wait for USB PLL */
	while (!(sysctrl_readl(RZN1_SYSCTRL_REG_USBSTAT) & (1 << RZN1_SYSCTRL_REG_USBSTAT_PLL_LOCK)))
		;

#if defined(CONFIG_CADENCE_QSPI)
	/* Enable QSPI */
	rzn1_clk_set_gate(RZN1_CLK_REF_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_QSPI0_ID, 1);
#endif

#if defined(CONFIG_NAND_CADENCE_EVATRONIX)
	/* Enable NAND */
	rzn1_clk_set_gate(RZN1_CLKB_NAND_ID, 1);
	rzn1_clk_set_gate(RZN1_CLKA_NAND_ID, 1);
#endif

#ifndef CONFIG_SPL_BUILD
#if defined(CONFIG_DESIGNWARE_ETH)
	/* Enable Ethernet MAC1 */
	rzn1_clk_set_gate(RZN1_ACLK_MAC1_ID, 1);
#endif

#if defined(CONFIG_DW_I2C)
	rzn1_clk_set_gate(RZN1_PCLK_I2C0_ID, 1);

	/* PCA9698 I2C port expanders */
	#define IO_NR(x,y)	(((x) << 8) + (y))

	/* Read the LCES Board ID */
	pca9698_direction_input(0x4e >> 1, IO_NR(4, 0)); /* BOARD_LCES_ID0 */
	pca9698_direction_input(0x4e >> 1, IO_NR(4, 1)); /* BOARD_LCES_ID1 */
	pca9698_direction_input(0x4e >> 1, IO_NR(4, 2)); /* BOARD_LCES_ID2 */
	val = pca9698_get_value(0x4e >> 1, IO_NR(4, 0));
	val |= pca9698_get_value(0x4e >> 1, IO_NR(4, 1)) << 1;
	val |= pca9698_get_value(0x4e >> 1, IO_NR(4, 2)) << 2;
	printf("LCES Board ID is 0x%x\n", val);

	/* Read the USB1 Host/Function switch (W5) */
	pca9698_direction_input(0x4c >> 1, IO_NR(3, 0)); /* SEL0_USB1-CONN */
	val = pca9698_get_value(0x4c >> 1, IO_NR(3, 0));
	printf("USB1 Host/Function switch (W5) is set to %s\n", val ? "Host" : "Func");

	/* TODO need to check this works */
	/* Don't know what pins we need to set up and may need to set some other pins */
#if 0
	/* U188 */
	pca9698_direction_output(0x48 >> 1, IO_NR(0, 7), 0); /* BUF_ENABLE */
	pca9698_direction_output(0x48 >> 1, IO_NR(2, 0), 0); /* SEL0_BUF_REF_CLK */
	pca9698_direction_output(0x48 >> 1, IO_NR(2, 1), 0); /* SEL1_BUF_REF_CLK */
	pca9698_direction_output(0x48 >> 1, IO_NR(3, 0), 0); /* SEL1_MAC1_ADD-SMI2 */
	pca9698_direction_output(0x48 >> 1, IO_NR(3, 1), 0); /* SEL0_MAC1_ADD-SMI2 */
	pca9698_direction_output(0x48 >> 1, IO_NR(3, 2), 0); /* SEL1_MAC1 */
	pca9698_direction_output(0x48 >> 1, IO_NR(3, 3), 0); /* SEL0_MAC1 */
	pca9698_direction_output(0x48 >> 1, IO_NR(3, 4), 0); /* SEL1_MAC1_CLK */
	pca9698_direction_output(0x48 >> 1, IO_NR(3, 5), 0); /* SEL0_MAC1_CLK */
	pca9698_direction_output(0x48 >> 1, IO_NR(3, 6), 0); /* SEL1_PHY0_CLK */
	pca9698_direction_output(0x48 >> 1, IO_NR(3, 7), 0); /* SEL0_SMI2 */

	/* U189 */
	pca9698_direction_output(0x4c >> 1, IO_NR(1, 3), 0); /* SEL0_UART1 */
	pca9698_direction_output(0x4c >> 1, IO_NR(1, 4), 0); /* SEL0_QUADSPI */
	pca9698_direction_output(0x4c >> 1, IO_NR(1, 6), 0); /* SEL0_NAND */
	pca9698_direction_output(0x4c >> 1, IO_NR(1, 7), 0); /* SEL1_NAND */
#endif
#endif	/* CONFIG_DW_I2C */
#endif	/* CONFIG_SPL_BUILD */
	return 0;
}

extern const u32 ddr_00_87_async[];
extern const u32 ddr_350_374_async[];

int dram_init(void)
{
#if defined(CONFIG_CADENCE_DDR_CTRL)

	/* Enable DDR Controller clock and FlexWAY connection */
	rzn1_clk_set_gate(RZN1_CLK_DDR_ID, 1);
	rzn1_clk_set_gate(RZN1_ACLK_AXIY_ID, 1);

	/* DDR Controller is always in ASYNC mode */
	cdns_ddr_ctrl_init((u32 *)RZN1_DDR_BASE, 1,
			   ddr_00_87_async, ddr_350_374_async,
			   RZN1_V_DDR_BASE, SZ_256M);

#if defined(CONFIG_CADENCE_DDR_CTRL_ENABLE_ECC)
	/*
	 * Any read before a write will trigger an ECC un-correctable error,
	 * causing a data abort. However, this is also true for any read with a
	 * size less than the AXI bus width. So, the only sensible solution is
	 * to write to all of DDR now and take the hit...
	 */
	 memset((void *)RZN1_V_DDR_BASE, 0xff, SZ_128M);

	/*
	 * Note: The call to get_ram_size() below checks to see what memory is
	 * actually there, but it reads before writing which would also trigger
	 * an ECC un-correctable error if we don't write to all of DDR.
	 */
#endif
	gd->ram_size = get_ram_size((void *)CONFIG_SYS_SDRAM_BASE,
					    CONFIG_SYS_SDRAM_SIZE);
#endif

	return 0;
}

#if defined(CONFIG_DESIGNWARE_ETH)
/* RIN Ether Accessory (Switch Control) regs */
#define MODCTRL		0x8

#if defined(CONFIG_MT5PT_SWITCH)
/* Simple setup, all ports enabled at the same speed */
static void eval_switch_setup(int speed)
{
	rzn1_mt5pt_switch_init();

	rzn1_switch_setup_port_speed(0, speed, 1);
	rzn1_switch_setup_port_speed(1, speed, 1);
	rzn1_switch_setup_port_speed(2, speed, 1);
	rzn1_switch_setup_port_speed(3, speed, 1);
	rzn1_switch_setup_port_speed(4, speed, 1);
}
#endif	/* CONFIG_MT5PT_SWITCH */

/*
 * RIN RGMII/RMII Convertor setup all
 * Simple setup, all PHY convertors enabled with the same mode.
 * if_type: Type of PHY interface, see phy_interface_t
 * speed: 10, 100, 1000
 */
static void eval_rgmii_rmii_convertor_setup(u32 if_type, int speed)
{
	/* RGMII/RMII Convertor 1 can only be connected to GMAC1 */
	rzn1_rin_rgmii_rmii_conv_setup_phy(1, if_type, speed);

	/* RGMII/RMII Convertor 2 can be connected directly to GMAC2, or
	 * the 5pt-switch port 3 */
	rzn1_rin_rgmii_rmii_conv_setup_phy(2, if_type, speed);

	/* For the other RGMII/RMII Convertors, they can be connected to the
	 * 5pt-switch port number (5 - PHY-nr), i.e. PHY 3 to switch port 2,
	 * PHY 4 to switch port 1 and PHY 5 to switch port 0.
	 */
	rzn1_rin_rgmii_rmii_conv_setup_phy(3, if_type, speed);
	rzn1_rin_rgmii_rmii_conv_setup_phy(4, if_type, speed);
	rzn1_rin_rgmii_rmii_conv_setup_phy(5, if_type, speed);
}

int board_eth_init(bd_t *bis)
{
	u32 interface = PHY_INTERFACE_MODE_RGMII;
	int speed = 1000;
	int ret = 0;

	rzn1_rin_init();

	eval_rgmii_rmii_convertor_setup(interface, speed);

#if defined(CONFIG_MT5PT_SWITCH)
	/* RIN: Mode Control - GMAC2 on all Switch ports */
	writel(0x13, RZN1_SWITCH_CTRL_REG_BASE + MODCTRL);

	eval_switch_setup(speed);
#else
	/* RIN: Mode Control - Connect GMII/RMII2 direct to MAC2 port */
	writel(0x8, RZN1_SWITCH_CTRL_REG_BASE + MODCTRL);
#endif

	rzn1_rin_reset_clks();

	/* Enable Ethernet MAC2 (these regs are 0 indexed) */
	rzn1_clk_set_gate(RZN1_ACLK_MAC1_ID, 1);
	rzn1_clk_reset(RZN1_ACLK_MAC1_ID);

	if (designware_initialize(RZN1_GMAC1_BASE, interface) >= 0)
		ret++;
	return ret;
}

int board_phy_config(struct phy_device *phydev)
{
	/* TODO Marvell 88E1512 RGMII clock delays */

	if (phydev->drv->config)
		phydev->drv->config(phydev);

	return 0;
}
#endif	/* CONFIG_DESIGNWARE_ETH */

void spl_board_init(void)
{
	arch_cpu_init();
	board_init();
	preloader_console_init();
	dram_init();
}
