/*
 * SCIT board (RZ/N1 device) init
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
	rzn1_pinmux_select(25, RZN1_FUNC_UART0, 0);
	rzn1_pinmux_select(26, RZN1_FUNC_UART0, 0);

	/* QSPI pins */
	for (i = 73; i <= 79; i++)
		rzn1_pinmux_select(i, RZN1_FUNC_QSPI, 0);
	rzn1_pinmux_select(149, RZN1_FUNC_QSPI, 0);	/* QSPI CS */
	rzn1_pinmux_select(150, RZN1_FUNC_QSPI, 0);	/* QSPI CS */

	/* NAND pins */
	for (i = 80; i <= 93; i++)
		rzn1_pinmux_select(i, RZN1_FUNC_CLK_ETH_NAND, 0);
	rzn1_pinmux_select(94, RZN1_FUNC_CLK_ETH_NAND, RZN1_PULL_UP);	/* NAND 0 RDY/BSY */
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

#if defined(CONFIG_SDHCI)
#include <mmc.h>

static int rzn1_sdhci_init(u32 regbase)
{
	struct sdhci_host *host = NULL;

	host = (struct sdhci_host *)malloc(sizeof(struct sdhci_host));
	if (!host) {
		printf("%s: sdhci_host malloc fail\n", __func__);
		return 1;
	}

	host->name = "rzn1_sdhci";
	host->ioaddr = (void *)regbase;
	host->quirks = SDHCI_QUIRK_NO_CD | SDHCI_QUIRK_BROKEN_VOLTAGE |
		       SDHCI_QUIRK_WAIT_SEND_CMD | SDHCI_QUIRK_BROKEN_R1B;
	host->voltages = MMC_VDD_32_33 | MMC_VDD_33_34;
	host->version = sdhci_readw(host, SDHCI_HOST_VERSION);

	host->host_caps = MMC_MODE_HC;

	add_sdhci(host, 1600000, 400000);
	return 0;
}

int board_mmc_init(bd_t *bd)
{
	int ret = 0;

#if defined(CONFIG_RZN1_SDHCI0)
	ret |= rzn1_sdhci_init(RZN1_SDIO0_BASE);
#endif

#if defined(CONFIG_RZN1_SDHCI1)
	ret |= rzn1_sdhci_init(RZN1_SDIO1_BASE);
#endif

	return ret;
}
#endif	/* CONFIG_SDHCI */

int board_init(void)
{
	/*
	 * Enable USB Function on port 1 as the Linux kernel doesn't deal with
	 * these irregular register bits
	 */
	sysctrl_writel((1 << RZN1_SYSCTRL_REG_CFG_USB_FRCLK48MOD), RZN1_SYSCTRL_REG_CFG_USB);
	/* Wait for USB PLL */
	while (!(sysctrl_readl(RZN1_SYSCTRL_REG_USBSTAT) & (1 << RZN1_SYSCTRL_REG_USBSTAT_PLL_LOCK)))
		;

	/* Set SCIT SDIO clock to 1.6MHz */
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_SDIO0DIV, 10);
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_SDIO1DIV, 10);
	sysctrl_writel(1, RZN1_SYSCTRL_REG_CFG_SDIO0);
	sysctrl_writel(1, RZN1_SYSCTRL_REG_CFG_SDIO1);

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

#if defined(CONFIG_RZN1_SDHCI0)
	/* Enable SDHC0 */
	rzn1_clk_set_gate(RZN1_CLK_XIN_SDIO0_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_HOST_SDIO0_ID, 1);
	rzn1_clk_reset(RZN1_CLK_HOST_SDIO0_ID);
#endif

#if defined(CONFIG_RZN1_SDHCI1)
	/* Enable SDHC1 */
	rzn1_clk_set_gate(RZN1_CLK_XIN_SDIO1_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_HOST_SDIO1_ID, 1);
	rzn1_clk_reset(RZN1_CLK_HOST_SDIO1_ID);
#endif
#endif

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
static void scit_switch_setup(int speed)
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
static void scit_rgmii_rmii_convertor_setup(u32 if_type, int speed)
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

#if defined(CONFIG_MACH_SCIT)
/* SCIT has a special register to control signals going to the Vitesse
 * PHY. It's located where the SERCOS IP block would normally be, so
 * SERCOS clocks and reset are needed to access it.
 */
#define SCIT_ETH_PHY_SETUP RZN1_SERCOS_BASE
#define SEPS_COMA_MODE		(1 << 0)
#define SEPS_RST		(1 << 1)
#define SEPS_MAC0_125MHz	(1 << 2)
#define SEPS_MAC1_125MHz	(1 << 3)

static void scit_eth_phy_early(void)
{
	u32 coma;

	/* SERCOS clocks */
	rzn1_clk_set_gate(RZN1_HCLK_S3_CLK100G_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_S3_CLK50_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_S3_CLK100_ID, 1);
	rzn1_clk_reset(RZN1_HCLK_S3_CLK100G_ID);
	rzn1_clk_reset(RZN1_CLK_S3_CLK50_ID);

	/* 1. COMA_MODE active, drive high */
	coma = SEPS_COMA_MODE;
	writel(coma, SCIT_ETH_PHY_SETUP);

	/* 3. Apply RefClk */
	coma |= SEPS_MAC0_125MHz | SEPS_MAC1_125MHz;
	writel(coma, SCIT_ETH_PHY_SETUP);

	/* 4. Release reset, drive high */
	coma |= SEPS_RST;
	writel(coma, SCIT_ETH_PHY_SETUP);

	/* 5. Wait 120ms */
	udelay(120000);
}

static void scit_eth_phy_late(void)
{
	u32 coma;

	/* Release COMA_MODE pin, drive low */
	coma = readl(SCIT_ETH_PHY_SETUP);
	coma &= ~SEPS_COMA_MODE;
	writel(coma, SCIT_ETH_PHY_SETUP);
}
#endif

int board_eth_init(bd_t *bis)
{
	u32 interface = PHY_INTERFACE_MODE_RGMII;
	int speed = 1000;
	int ret = 0;

	scit_eth_phy_early();

	rzn1_rin_init();

	scit_rgmii_rmii_convertor_setup(interface, speed);

#if defined(CONFIG_MT5PT_SWITCH)
	/* RIN: Mode Control - GMAC2 on all Switch ports */
	writel(0x13, RZN1_SWITCH_CTRL_REG_BASE + MODCTRL);

	scit_switch_setup(speed);
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

#define VITESSE_8552_EXT_PAGE_ACCESS	0x1f
#define VITESSE_8552_E2_RGMII		0x12

int board_phy_config(struct phy_device *phydev)
{
	/* Vitesse 8552 RGMII clock delays reg 18E2
	 * Bits 6:4 rgmii_skew_tx
	 * Bits 3:1 rgmii_skew_rx
	 * Bit    0 read only
	 * 0 = 0.2ns
	 * 1 = 0.8ns
	 * 2 = 1.1ns
	 * 3 = 1.7ns
	 * 4 = 2.0ns
	 * 5 = 2.3ns
	 * 6 = 2.6ns
	 * 7 = 3.4ns
	 */
	u8 skew = (7 << 4) | (0 << 1);

	/* Switch to Extended 2 page */
	phy_write(phydev, MDIO_DEVAD_NONE, VITESSE_8552_EXT_PAGE_ACCESS, 2);

	/* write the RGMII clock delays */
	phy_write(phydev, MDIO_DEVAD_NONE, VITESSE_8552_E2_RGMII, skew);

	if (phydev->drv->config)
		phydev->drv->config(phydev);

	scit_eth_phy_late();

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
