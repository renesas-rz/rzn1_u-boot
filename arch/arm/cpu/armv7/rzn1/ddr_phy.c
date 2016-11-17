/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <asm/io.h>
#include <common.h>
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-utils.h"
#include "renesas/rzn1-clocks.h"
#include "renesas/rzn1-sysctrl.h"

#define FUNCCTRL	0x00
#define  FUNCCTRL_MASKSDLOFS	(0x18 << 16)
#define  FUNCCTRL_DVDDQ_1_5V	(1 << 8)
#define  FUNCCTRL_RESET_N	(1 << 0)
#define DLLCTRL		0x04
#define  DLLCTRL_ASDLLOCK	(1 << 26)
#define  DLLCTRL_MFSL_500MHz	(2 << 1)
#define  DLLCTRL_MDLLSTBY	(1 << 0)
#define ZQCALCTRL	0x08
#define  ZQCALCTRL_ZQCALEND	(1 << 30)
#define  ZQCALCTRL_ZQCALRSTB	(1 << 0)
#define ZQODTCTRL	0x0c
#define RDCTRL		0x10
#define RDTMG		0x14
#define FIFOINIT	0x18
#define  FIFOINIT_RDPTINITEXE	(1 << 8)
#define  FIFOINIT_WRPTINITEXE	(1 << 0)
#define OUTCTRL		0x1c
#define  OUTCTRL_ADCMDOE	(1 << 0)
#define WLCTRL1		0x40
#define  WLCTRL1_WLSTR		(1 << 24)
#define DQCALOFS1	0xe8

/* DDR PHY setup */
void ddr_phy_init(int ddr_type)
{
	u32 val;

	/* Disable DDR Controller clock and FlexWAY connection */
	rzn1_clk_set_gate(RZN1_HCLK_DDRC_ID, 0);
	rzn1_clk_set_gate(RZN1_CLK_DDRC_ID, 0);

	rzn1_clk_reset_state(RZN1_HCLK_DDRC_ID, 0);
	rzn1_clk_reset_state(RZN1_CLK_DDRC_ID, 0);

	/* Enable DDR Controller clock and FlexWAY connection */
	rzn1_clk_set_gate(RZN1_CLK_DDRC_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_DDRC_ID, 1);

	/* DDR PHY Soft reset assert */
	writel(FUNCCTRL_MASKSDLOFS | FUNCCTRL_DVDDQ_1_5V, RZN1_DDRPHY_BASE + FUNCCTRL);

	rzn1_clk_reset_state(RZN1_CLK_DDRC_ID, 1);
	rzn1_clk_reset_state(RZN1_HCLK_DDRC_ID, 1);

	/* DDR PHY setup */
	writel(DLLCTRL_MFSL_500MHz | DLLCTRL_MDLLSTBY, RZN1_DDRPHY_BASE + DLLCTRL);
	writel(0x00000186, RZN1_DDRPHY_BASE + ZQCALCTRL);
	if (ddr_type == RZN1_DDR3_DUAL_BANK)
		writel(0xAB330031, RZN1_DDRPHY_BASE + ZQODTCTRL);
	else if (ddr_type == RZN1_DDR3_SINGLE_BANK)
		writel(0xAB320051, RZN1_DDRPHY_BASE + ZQODTCTRL);
	else /* DDR2 */
		writel(0xAB330071, RZN1_DDRPHY_BASE + ZQODTCTRL);
	writel(0xB545B544, RZN1_DDRPHY_BASE + RDCTRL);
	writel(0x000000B0, RZN1_DDRPHY_BASE + RDTMG);
	writel(0x020A0806, RZN1_DDRPHY_BASE + OUTCTRL);
	if (ddr_type == RZN1_DDR3_DUAL_BANK)
		writel(0x80005556, RZN1_DDRPHY_BASE + WLCTRL1);
	else
		writel(0x80005C5D, RZN1_DDRPHY_BASE + WLCTRL1);
	writel(0x00000101, RZN1_DDRPHY_BASE + FIFOINIT);
	writel(0x00004545, RZN1_DDRPHY_BASE + DQCALOFS1);

	/* Step 9 MDLL reset release */
	val = readl(RZN1_DDRPHY_BASE + DLLCTRL);
	val &= ~DLLCTRL_MDLLSTBY;
	writel(val, RZN1_DDRPHY_BASE + DLLCTRL);

	/* Step 12 Soft reset release */
	val = readl(RZN1_DDRPHY_BASE + FUNCCTRL);
	val |= FUNCCTRL_RESET_N;
	writel(val, RZN1_DDRPHY_BASE + FUNCCTRL);

	/* Step 13 FIFO pointer initialize */
	writel(FIFOINIT_RDPTINITEXE | FIFOINIT_WRPTINITEXE, RZN1_DDRPHY_BASE + FIFOINIT);

	/* Step 14 Execute ZQ Calibration */
	val = readl(RZN1_DDRPHY_BASE + ZQCALCTRL);
	val |= ZQCALCTRL_ZQCALRSTB;
	writel(val, RZN1_DDRPHY_BASE + ZQCALCTRL);

	/* Step 15 Wait for 200us or more, or wait for DFIINITCOMPLETE to be "1" */
	while (!(readl(RZN1_DDRPHY_BASE + DLLCTRL) & DLLCTRL_ASDLLOCK))
		;
	while (!(readl(RZN1_DDRPHY_BASE + ZQCALCTRL) & ZQCALCTRL_ZQCALEND))
		;

	/* Step 16 Enable Address and Command output */
	val = readl(RZN1_DDRPHY_BASE + OUTCTRL);
	val |= OUTCTRL_ADCMDOE;
	writel(val, RZN1_DDRPHY_BASE + OUTCTRL);

	/* Step 17 Wait for 200us or more(from MRESETB=0) */
	udelay(200);
}

void ddr_phy_enable_wl(void)
{
	u32 val;

	/* Step 26 (Set Write Leveling) */
	val = readl(RZN1_DDRPHY_BASE + WLCTRL1);
	val |= WLCTRL1_WLSTR;
	writel(val, RZN1_DDRPHY_BASE + WLCTRL1);
}
