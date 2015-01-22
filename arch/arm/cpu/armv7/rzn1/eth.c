/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <asm/io.h>
#include <common.h>
#include <miiphy.h>
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-utils.h"
#include "renesas/rzn1-clocks.h"
#include "renesas/rzn1-sysctrl.h"

/* RIN Ether Accessory (Switch Control) regs */
#define PRCMD		0x0
#define IDCODE		0x4
#define MODCTRL		0x8
#define PTP_MODE_CTRL	0xc
#define PHY_LINK_MODE	0x14

#if defined(CONFIG_MT5PT_SWITCH)
/* RIN SwitchCore regs */
#define RIN_SWCTRL		0x304		/* SwitchCore Control */
#define  RIN_SWCTRL_MPBS_10(x)		(((0 << 4) | (1 << 0)) << (x))
#define  RIN_SWCTRL_MPBS_100(x)		(((0 << 4) | (0 << 0)) << (x))
#define  RIN_SWCTRL_MPBS_1000(x)	(((1 << 4) | (0 << 0)) << (x))
#define  RIN_SWCTRL_MPBS_MASK(x)	(((1 << 4) | (1 << 0)) << (x))
#define RIN_SWDUPC		0x308		/* SwitchCore Duplex Mode */

void rzn1_rin_switchcore_setup(int port, int speed)
{
	u32 val;

	val = readl(RZN1_SWITCH_CTRL_REG_BASE + RIN_SWCTRL);
	val &= ~RIN_SWCTRL_MPBS_MASK(port);

	if (speed == 1000)
		val |= RIN_SWCTRL_MPBS_1000(port);
	else if (speed == 100)
		val |= RIN_SWCTRL_MPBS_100(port);
	else
		val |= RIN_SWCTRL_MPBS_10(port);

	writel(val, RZN1_SWITCH_CTRL_REG_BASE + RIN_SWCTRL);
}

/* MoreThanIP 5pt Switch regs */
#define MT5PT_REVISION		0x0
#define MT5PT_SCRATCH		0x4
#define MT5PT_PORT_ENA		0x8
#define  MT5PT_PORT_ENA_TX(x)	(1 << ((x)+16))
#define  MT5PT_PORT_ENA_RX(x)	(1 << (x))
#define  MT5PT_PORT_ENA_TXRX(x)	(MT5PT_PORT_ENA_TX(x) | MT5PT_PORT_ENA_RX(x))
#define MT5PT_AUTH_PORTn(x)	(0x240 + (x) * 4)
#define  MT5PT_AUTH_PORT_AUTHORIZED	(1 << 0)
#define  MT5PT_AUTH_PORT_CONTROLLED	(1 << 1)
#define  MT5PT_AUTH_PORT_EAPOL_EN	(1 << 2)
#define  MT5PT_AUTH_PORT_GUEST		(1 << 3)
#define  MT5PT_AUTH_PORT_EAPOL_PORT(x)	((x) << 12)
#define MT5PT_MAC_CMD_CFGn(x)	(0x800 + (x) * 1024)
#define  MT5PT_TX_ENA			(1 << 0)
#define  MT5PT_RX_ENA			(1 << 1)
#define  MT5PT_MBPS_1000		(1 << 3)

void rzn1_mt5pt_switch_enable_port(int port)
{
	u32 val;

	val = readl(RZN1_SWITCH_BASE + MT5PT_AUTH_PORTn(port));
	val |= MT5PT_AUTH_PORT_AUTHORIZED;
	writel(val, RZN1_SWITCH_BASE + MT5PT_AUTH_PORTn(port));

	val = readl(RZN1_SWITCH_BASE + MT5PT_PORT_ENA);
	val |= MT5PT_PORT_ENA_TXRX(port);
	writel(val, RZN1_SWITCH_BASE + MT5PT_PORT_ENA);
}

void rzn1_mt5pt_switch_setup_port_speed(int port, int speed)
{
	u32 val;

	val = readl(RZN1_SWITCH_BASE + MT5PT_MAC_CMD_CFGn(port));
	val &= ~MT5PT_MBPS_1000;
	if (speed == 1000)
		val |= MT5PT_MBPS_1000;
	writel(val, RZN1_SWITCH_BASE + MT5PT_MAC_CMD_CFGn(port));
}

void rzn1_switch_setup_port_speed(int port, int speed, int enable)
{
	/*
	 * The MoreThanIP 5 port switch has 5 ports. Port 4 is also known as
	 * PORTIN and is connected to GMAC2 or MAC RTOS. The other 4 ports (0
	 * to 3) are downstream and can connect to a PHY.
	 */
	rzn1_mt5pt_switch_setup_port_speed(port, speed);

	/* The RIN SwitchCore has registers that cover ports 0 to 3 */
	if (port <= 3)
		rzn1_rin_switchcore_setup(port, speed);

	if (enable)
		rzn1_mt5pt_switch_enable_port(port);
}

void rzn1_mt5pt_switch_init(void)
{
	/* 5pt Switch clock divider setting */
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_SWITCHDIV, 5);

	/* Enable 5pt Switch clocks */
	rzn1_clk_set_gate(RZN1_CLK_SWITCH_SX_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_CPU_ID, 1);
	rzn1_clk_reset(RZN1_CLK_SWITCH_SX_ID);
}
#endif	/* CONFIG_MT5PT_SWITCH */

/* RIN RGMII/RMII Convertor regs */
#define CONVCTRLn(x)	(0x100 + ((x)-1) * 4)
#define  CONVCTRL_10_MBPS		(0 << 0)
#define  CONVCTRL_100_MBPS		(1 << 0)
#define  CONVCTRL_1000_MBPS		(1 << 1)
#define  CONVCTRL_MII			(0 << 2)
#define  CONVCTRL_RMII			(1 << 2)
#define  CONVCTRL_RGMII			(1 << 3)
#define  CONVCTRL_REF_CLK_OUT		(1 << 4)
#define  CONVCTRL_HALF_DUPLEX		(0 << 8)
#define  CONVCTRL_FULL_DUPLEX		(1 << 8)
#define CONVRST		0x114
#define  PHYIF_RST(x)		(1 << (x))

/*
 * RIN RGMII/RMII Convertor setup one
 * phy: 1..5
 * if_type: Type of PHY interface, see phy_interface_t
 * speed: 10, 100, 1000
 */
void rzn1_rin_rgmii_rmii_conv_setup_phy(int phy, u32 if_type, int speed)
{
	u32 val = CONVCTRL_FULL_DUPLEX;

	if (if_type == PHY_INTERFACE_MODE_RGMII)
		val |= CONVCTRL_RGMII;
	else if (if_type == PHY_INTERFACE_MODE_RMII)
		val |= CONVCTRL_RMII;
	else if (if_type == PHY_INTERFACE_MODE_MII)
		val |= CONVCTRL_MII;

	if (speed == 1000)
		val |= CONVCTRL_1000_MBPS;
	else if (speed == 100)
		val |= CONVCTRL_100_MBPS;
	else if (speed == 10)
		val |= CONVCTRL_10_MBPS;

	writel(val, RZN1_SWITCH_CTRL_REG_BASE + CONVCTRLn(phy));

	/* clear reset */
	val = readl(RZN1_SWITCH_CTRL_REG_BASE + CONVRST);
	val |= PHYIF_RST((phy - 1));
	writel(val, RZN1_SWITCH_CTRL_REG_BASE + CONVRST);
}

void rzn1_rin_init(void)
{
	/* Enable RIN clocks */
	rzn1_clk_set_gate(RZN1_HCLK_BUS_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_SWITCH_RG_ID, 1);

	/* 25MHz clock for MII */
	rzn1_clk_set_gate(RZN1_CLK25_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK50MHZ_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK100_ID, 1);

	/* RIN: Unprotect register writes */
	writel(0x00a5, RZN1_SWITCH_CTRL_REG_BASE + PRCMD);
	writel(0x0001, RZN1_SWITCH_CTRL_REG_BASE + PRCMD);
	writel(0xfffe, RZN1_SWITCH_CTRL_REG_BASE + PRCMD);
	writel(0x0001, RZN1_SWITCH_CTRL_REG_BASE + PRCMD);

	/* Set PTP to use 25MHz PLL clock */
	writel(0x4, RZN1_SWITCH_CTRL_REG_BASE + PTP_MODE_CTRL);
}

void rzn1_rin_reset_clks(void)
{
	u32 val;

	/* Clear ETH and CLK25 resets */
	val = readl(RZN1_SYSTEM_CTRL_BASE + RZN1_SYSCTRL_REG_PWRCTRL_SWITCHCTRL);
	val |= (1 << RZN1_SYSCTRL_REG_PWRCTRL_SWITCHCTRL_RSTN_ETH);
	val |= (1 << RZN1_SYSCTRL_REG_PWRCTRL_SWITCHCTRL_RSTN_CLK25);
	writel(val, RZN1_SYSTEM_CTRL_BASE + RZN1_SYSCTRL_REG_PWRCTRL_SWITCHCTRL);
}
