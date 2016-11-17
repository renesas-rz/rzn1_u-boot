/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	BSD-2-Clause
 */
#include <asm/io.h>
#include <common.h>
#include <miiphy.h>
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-utils.h"
#include "renesas/rzn1-clocks.h"
#include "renesas/rzn1-sysctrl.h"

/* MoreThanIP 5pt Switch regs */
#define MT5PT_REVISION		0x0
#define MT5PT_SCRATCH		0x4
#define MT5PT_PORT_ENA		0x8
#define  MT5PT_PORT_ENA_TX(x)		(1 << ((x) + 16))
#define  MT5PT_PORT_ENA_RX(x)		(1 << (x))
#define  MT5PT_PORT_ENA_TXRX(x)		(MT5PT_PORT_ENA_TX(x) | MT5PT_PORT_ENA_RX(x))
#define MT5PT_AUTH_PORT(x)	(0x240 + (x) * 4)
#define  MT5PT_AUTH_PORT_AUTHORIZED	(1 << 0)
#define  MT5PT_AUTH_PORT_CONTROLLED	(1 << 1)
#define  MT5PT_AUTH_PORT_EAPOL_EN	(1 << 2)
#define  MT5PT_AUTH_PORT_GUEST		(1 << 3)
#define  MT5PT_AUTH_PORT_EAPOL_PORT(x)	((x) << 12)
#define MT5PT_MAC_CMD_CFGn(x)	(0x808 + (x) * 0x400)
#define  MT5PT_TX_ENA			(1 << 0)
#define  MT5PT_RX_ENA			(1 << 1)
#define  MT5PT_MBPS_1000		(1 << 3)
#define MT5PT_MAC_FRM_LENGTHn(x)	(0x814 + (x) * 0x400)

void rzn1_mt5pt_switch_enable_port(int port)
{
	u8 *regs = (u8 *)RZN1_SWITCH_BASE;
	u32 val;

	val = readl(regs + MT5PT_AUTH_PORT(port));
	val |= MT5PT_AUTH_PORT_AUTHORIZED;
	writel(val, regs + MT5PT_AUTH_PORT(port));

	val = readl(regs + MT5PT_PORT_ENA);
	val |= MT5PT_PORT_ENA_TXRX(port);
	writel(val, regs + MT5PT_PORT_ENA);

	/* Max frame size */
	writel(9224, regs + MT5PT_MAC_FRM_LENGTHn(port));
}

void rzn1_mt5pt_switch_setup_port_speed(int port, int speed)
{
	u8 *regs = (u8 *)RZN1_SWITCH_BASE;
	u32 val;

	val = readl(regs + MT5PT_MAC_CMD_CFGn(port));
	val &= ~MT5PT_MBPS_1000;
	if (speed == SPEED_1000)
		val |= MT5PT_MBPS_1000;
	writel(val, regs + MT5PT_MAC_CMD_CFGn(port));
}

void rzn1_switch_setup_port_speed(int port, int speed, int enable)
{
	/*
	 * The MoreThanIP 5 port switch has 5 ports. Port 4 is also known as
	 * PORTIN and is connected to GMAC2 or MAC RTOS. The other 4 ports (0
	 * to 3) are downstream and can connect to a PHY.
	 */
	rzn1_mt5pt_switch_setup_port_speed(port, speed);

	if (enable)
		rzn1_mt5pt_switch_enable_port(port);
}

void rzn1_mt5pt_switch_init(void)
{
	/* 5pt Switch clock divider setting */
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_SWITCHDIV, 5);

	/* Enable 5pt Switch clocks */
	rzn1_clk_set_gate(RZN1_CLK_SWITCH_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_SWITCH_ID, 1);
	rzn1_clk_reset(RZN1_CLK_SWITCH_ID);
}

/* RIN Ether Accessory (Switch Control) regs */
#define PRCMD			0x0		/* Ethernet Protect */
#define IDCODE			0x4		/* EtherSwitch IDCODE */
#define MODCTRL			0x8		/* Mode Control */
#define PTP_MODE_CTRL		0xc		/* PTP Mode Control */
#define PHY_LINK_MODE		0x14		/* Ethernet PHY Link Mode */

/* RIN RGMII/RMII Converter regs */
#define CONVCTRL(x)		(0x100 + ((x)) * 4) /* RGMII/RMII Converter */
#define  CONVCTRL_10_MBPS		(0 << 0)
#define  CONVCTRL_100_MBPS		(1 << 0)
#define  CONVCTRL_1000_MBPS		(1 << 1)
#define  CONVCTRL_MII			(0 << 2)
#define  CONVCTRL_RMII			(1 << 2)
#define  CONVCTRL_RGMII			(1 << 3)
#define  CONVCTRL_REF_CLK_OUT		(1 << 4)
#define  CONVCTRL_HALF_DUPLEX		(0 << 8)
#define  CONVCTRL_FULL_DUPLEX		(1 << 8)
#define CONVRST			0x114		/* RGMII/RMII Converter RST */
#define  PHYIF_RST(x)			(1 << (x))

/* RIN SwitchCore regs */
#define RIN_SWCTRL		0x304		/* SwitchCore Control */
#define  RIN_SWCTRL_MPBS_10(x)		(((0 << 4) | (1 << 0)) << (x))
#define  RIN_SWCTRL_MPBS_100(x)		(((0 << 4) | (0 << 0)) << (x))
#define  RIN_SWCTRL_MPBS_1000(x)	(((1 << 4) | (0 << 0)) << (x))
#define  RIN_SWCTRL_MPBS_MASK(x)	(((1 << 4) | (1 << 0)) << (x))
#define RIN_SWDUPC		0x308		/* SwitchCore Duplex Mode */
#define  RIN_SWDUPC_DUPLEX_MASK(x)	(1 << (x))
#define  RIN_SWDUPC_DUPLEX_FULL(x)	(1 << (x))

void rzn1_rin_prot_writel(u32 val, u32 reg)
{
	u8 *regs = (u8 *)RZN1_SWITCH_CTRL_REG_BASE;

	/* RIN: Unprotect register writes */
	writel(0x00a5, regs + PRCMD);
	writel(0x0001, regs + PRCMD);
	writel(0xfffe, regs + PRCMD);
	writel(0x0001, regs + PRCMD);

	writel(val, regs + reg);

	/* Enable protection */
	writel(0x0000, regs + PRCMD);
}

void rzn1_rin_switchcore_setup(int port, int full_duplex, int speed)
{
	u8 *regs = (u8 *)RZN1_SWITCH_CTRL_REG_BASE;
	u32 val;

	/* speed */
	val = readl(regs + RIN_SWCTRL);
	val &= ~RIN_SWCTRL_MPBS_MASK(port);

	if (speed == SPEED_1000)
		val |= RIN_SWCTRL_MPBS_1000(port);
	else if (speed == SPEED_100)
		val |= RIN_SWCTRL_MPBS_100(port);
	else
		val |= RIN_SWCTRL_MPBS_10(port);

	rzn1_rin_prot_writel(val, RIN_SWCTRL);

	/* duplex */
	val = readl(regs + RIN_SWDUPC);
	val &= ~RIN_SWDUPC_DUPLEX_MASK(port);
	if (full_duplex)
		val |= RIN_SWDUPC_DUPLEX_FULL(port);
	rzn1_rin_prot_writel(val, RIN_SWDUPC);
}

/*
 * RIN RGMII/RMII Converter set speed
 * RGMII/RMII Converter number: 0..4
 * speed: 10, 100, 1000
 */
void rzn1_rgmii_rmii_conv_speed(int phy, int full_duplex, int speed)
{
	u8 *regs = (u8 *)RZN1_SWITCH_CTRL_REG_BASE;
	u32 val = 0;

	val = readl(regs + CONVCTRL(phy));

	val &= CONVCTRL_MII | CONVCTRL_RMII | CONVCTRL_RGMII |
		CONVCTRL_REF_CLK_OUT;

	/* The interface type and speed bits are somewhat intertwined */
	if (val != CONVCTRL_MII) {
		if (speed == SPEED_1000)
			val |= CONVCTRL_1000_MBPS;
		else if (speed == SPEED_100)
			val |= CONVCTRL_100_MBPS;
		else if (speed == SPEED_10)
			val |= CONVCTRL_10_MBPS;
	}

	if (full_duplex)
		val |= CONVCTRL_FULL_DUPLEX;

	writel(val, regs + CONVCTRL(phy));

	if (phy > 0)
		rzn1_rin_switchcore_setup(4 - phy, full_duplex, speed);
}

/*
 * RIN RGMII/RMII Converter setup one
 * RGMII/RMII Converter number: 0..4
 * if_type: Type of PHY interface, see phy_interface_t
 */
void rzn1_rgmii_rmii_conv_setup(int phy, u32 if_type, int rmii_ref_clk_out)
{
	u8 *regs = (u8 *)RZN1_SWITCH_CTRL_REG_BASE;
	u32 val = 0;
	int is_rgmii = 0;

	switch (if_type) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val |= CONVCTRL_RGMII;
		is_rgmii = 1;
		break;
	case PHY_INTERFACE_MODE_RMII:
		val |= CONVCTRL_RMII;
		break;
	case PHY_INTERFACE_MODE_MII:
		val |= CONVCTRL_MII;
		break;
	};

	if (if_type == PHY_INTERFACE_MODE_RMII && rmii_ref_clk_out)
		val |= CONVCTRL_REF_CLK_OUT;

	writel(val, regs + CONVCTRL(phy));

	if (is_rgmii)
		rzn1_rgmii_rmii_conv_speed(phy, DUPLEX_FULL, SPEED_1000);
	else
		rzn1_rgmii_rmii_conv_speed(phy, DUPLEX_FULL, SPEED_100);

	/* reset */
	val = readl(regs + CONVRST);
	val &= ~PHYIF_RST(phy);
	rzn1_rin_prot_writel(val, CONVRST);
	udelay(1000);
	val |= PHYIF_RST(phy);
	rzn1_rin_prot_writel(val, CONVRST);
}

void rzn1_rin_init(void)
{
	/* Enable RIN clocks */
	rzn1_clk_set_gate(RZN1_HCLK_SWITCH_RG_ID, 1);

	/* Set PTP to use 25MHz PLL clock */
	rzn1_rin_prot_writel(0x4, PTP_MODE_CTRL);
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
