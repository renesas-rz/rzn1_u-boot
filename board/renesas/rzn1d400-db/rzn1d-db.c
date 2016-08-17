/*
 * Renesas RZN1-400  module (RZ/N1D device) base board
 *
 * (C) Copyright 2016 Renesas Electronics Europe Limited
 *
 * SPDX-License-Identifier:	GPL-2.0+
 *
 */
/*
 * WARNING! All hardware information (device and board) indexes start at 1,
 * whereas all software indexes start at 0. All comments in this file refer
 * to the hardware indexes.
 */

#include <common.h>
#include <miiphy.h>
#include <netdev.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <linux/sizes.h>
#include <malloc.h>
#include <pca9698.h>
#include <sdhci.h>
#include <spl.h>
#include <usb.h>
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

#define RZN1_MUX_MDIO(x,y)	RZN1_MUX_PNONE_4MA(x,y),
#define RZN1_MUX_RGMII(x,y)	RZN1_MUX_PNONE_4MA(x,y),
#define RZN1_MUX_RMII(x,y)	RZN1_MUX_PNONE_4MA(x,y),
#define RZN1_MUX_I2C1(x,y)	RZN1_MUX_PNONE_12MA(x,y),
#define RZN1_MUX_NAND(x,y)	RZN1_MUX_PNONE_4MA(x,y),
#define RZN1_MUX_QSPI0(x,y)	RZN1_MUX_PNONE_6MA(x,y),
#define RZN1_MUX_SDIO0(x,y)	RZN1_MUX_PUP_12MA(x,y),
#define RZN1_MUX_UART0(x,y)	RZN1_MUX_PNONE_4MA(x,y),
#define RZN1_MUX_USB(x,y)	RZN1_MUX_PNONE_4MA(x,y),
#define RZN1_MUX_GPIO(x,y)	RZN1_MUX_PNONE_4MA(x,y),
#define RZN1_MUX_GPIO0(x,y)	RZN1_MUX_PNONE_4MA(x,y),
#define RZN1_MUX_GPIO1(x,y)	RZN1_MUX_PNONE_4MA(x,y),
#define RZN1_MUX_SWITCH(x,y)	RZN1_MUX_PNONE_4MA(x,y),

/* Generated pin settings */
static const u32 rzn1_pins[] = {
#if defined(RZN1_ENABLE_ETHERNET) && !defined(CONFIG_SPL_BUILD)
	RZN1_MUX_RGMII(60, CLK_ETH_MII_RGMII_RMII)	/* RGMII_REFCLK */
	RZN1_MUX_RMII(61, CLK_ETH_NAND)	/* MII_REFCLK */

	RZN1_MUX_MDIO(150, ETH_MDIO)	/* MDIO_MDC[1] */
	RZN1_MUX_MDIO(151, ETH_MDIO)	/* MDIO[1] */
	RZN1_MUX_MDIO(152, ETH_MDIO_E1)	/* MDIO_MDC[2] */
	RZN1_MUX_MDIO(153, ETH_MDIO)	/* MDIO[2] */

	/* This is special 'virtual' pins for the MDIO multiplexing */
	RZN1_MUX_MDIO(RZN1_MDIO_BUS0, MDIO_MUX_MAC0)	/* MDIO0 internal */
	RZN1_MUX_MDIO(RZN1_MDIO_BUS1, MDIO_MUX_MAC1)	/* MDIO1 internal */

	RZN1_MUX_SWITCH(114, MAC_MTIP_SWITCH)	/* SWITCH_MII_LINK[2] */

#ifdef CONFIG_APPLY_ETH_PHY_RESET_PULSE
	RZN1_MUX_GPIO1(94, GPIO)	/* GPIO1B[25]: ETH_PHY_RESET */
#endif
#endif

#if defined(RZN1_ENABLE_I2C)
	RZN1_MUX_I2C1(115, I2C)	/* I2C1_SCL */
	RZN1_MUX_I2C1(116, I2C)	/* I2C1_SDA */
#endif

#if defined(RZN1_ENABLE_QSPI)
	RZN1_MUX_QSPI0(74, QSPI)	/* QSPI0_CS_N[0] */
	RZN1_MUX_QSPI0(75, QSPI)	/* QSPI0_IO[3] */
	RZN1_MUX_QSPI0(76, QSPI)	/* QSPI0_IO[2] */
	RZN1_MUX_QSPI0(77, QSPI)	/* QSPI0_IO[1] */
	RZN1_MUX_QSPI0(78, QSPI)	/* QSPI0_IO[0] */
	RZN1_MUX_QSPI0(79, QSPI)	/* QSPI0_CLK */
#endif

#if defined(RZN1_ENABLE_SDHC)
	RZN1_MUX_SDIO0(95, SDIO)	/* SDIO0_CMD */
	RZN1_MUX_SDIO0(96, SDIO)	/* SDIO0_CLK */
	RZN1_MUX_SDIO0(97, SDIO)	/* SDIO0_IO[0] */
	RZN1_MUX_SDIO0(98, SDIO)	/* SDIO0_IO[1] */
	RZN1_MUX_SDIO0(99, SDIO)	/* SDIO0_IO[2] */
	RZN1_MUX_SDIO0(100, SDIO)	/* SDIO0_IO[3] */
	RZN1_MUX_SDIO0(101, SDIO_E)	/* SDIO0_CD_N */
	RZN1_MUX_SDIO0(102, SDIO_E)	/* SDIO0_WP */
#endif

	RZN1_MUX_UART0(103, UART0_I)	/* UART0_TXD */
	RZN1_MUX_UART0(104, UART0_I)	/* UART0_RXD */

#if defined(RZN1_ENABLE_USBF) && !defined(CONFIG_SPL_BUILD)
	RZN1_MUX_USB(119, USB)	/* USB_PPON[1] */
	RZN1_MUX_USB(120, USB)	/* USB_OC[1] */
#endif
};

/* Called early during device initialisation */
void rzn1_setup_pinmux(void)
{
	u8 i;

#if defined(RZN1_ENABLE_ETHERNET) && !defined(CONFIG_SPL_BUILD)
	/* Enable all GMII 1 to 5 pins */
	for (i = 0; i <= 59; i++)
		rzn1_pinmux_set(RZN1_MUX_PNONE_4MA(i, CLK_ETH_MII_RGMII_RMII));
#endif

	for (i = 0; i < sizeof(rzn1_pins) / sizeof(rzn1_pins[0]); i++)
		rzn1_pinmux_set(rzn1_pins[i]);
}

/* Configure board specific clocks for the USB blocks */
int board_usb_init(int index, enum usb_init_type init)
{
	/* Configure device clocks, etc */
	return rzn1_usb_init(index, init);
}

#if defined(RZN1_ENABLE_SDHC)
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
	host->quirks = SDHCI_QUIRK_BROKEN_VOLTAGE | SDHCI_QUIRK_WAIT_SEND_CMD;
	host->voltages = MMC_VDD_32_33 | MMC_VDD_33_34;
	host->version = sdhci_readw(host, SDHCI_HOST_VERSION);
	host->host_caps = MMC_MODE_4BIT | MMC_MODE_HS_52MHz | MMC_MODE_HC;

	add_sdhci(host, 50000000, 0);
	return 0;
}

int board_mmc_init(bd_t *bd)
{
	return rzn1_sdhci_init(RZN1_SDIO0_BASE);
}
#endif	/* RZN1_ENABLE_SDHC */

int board_init(void)
{
#if defined(RZN1_ENABLE_QSPI)
	/* Enable QSPI */
	rzn1_clk_set_gate(RZN1_CLK_REF_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_QSPI0_ID, 1);
#endif

#if defined(RZN1_ENABLE_I2C) && !defined(CONFIG_SPL_BUILD)
	rzn1_clk_set_gate(RZN1_PCLK_I2C1_ID, 1);

	/* Enable I2C for EEPROM */
	if (pca9698_direction_output(0x20, 16, 1))
		printf("i2c failed! a\n");
	if (pca9698_direction_output(0x20, 17, 0))
		printf("i2c failed! b\n");
	if (pca9698_direction_output(0x20, 18, 0))
		printf("i2c failed! c\n");
#endif

#if defined(RZN1_ENABLE_SDHC)
	/* Enable SDHC1 */
	rzn1_clk_set_gate(RZN1_CLK_XIN_SDIO0_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_HOST_SDIO0_ID, 1);
	rzn1_clk_reset(RZN1_CLK_HOST_SDIO0_ID);
#endif

#if defined(RZN1_ENABLE_GPIO)
	/* Enable GPIO clock */
	rzn1_clk_set_gate(RZN1_PCLK_GPIO0_ID, 1);
	rzn1_clk_set_gate(RZN1_PCLK_GPIO1_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_GPIO2_ID, 1);
#endif

	board_usb_init(0, USB_INIT_DEVICE);

#ifdef CONFIG_ARMV7_NONSEC_AT_BOOT
	armv7_switch_nonsec();
#endif

	return 0;
}

extern u32 ddr_00_87_async[];
extern u32 ddr_350_374_async[];

void rzn1_ddr3_single_bank(void *ddr_ctrl_base)
{
	/* CS0 */
	cdns_ddr_set_mr1(ddr_ctrl_base, 0,
		MR1_ODT_IMPEDANCE_60_OHMS,
		MR1_DRIVE_STRENGTH_40_OHMS);
	cdns_ddr_set_mr2(ddr_ctrl_base, 0,
		MR2_DYNAMIC_ODT_OFF,
		MR2_SELF_REFRESH_TEMP_EXT);

	/* ODT_WR_MAP_CS0 = 1, ODT_RD_MAP_CS0 = 0 */
	cdns_ddr_set_odt_map(ddr_ctrl_base, 0, 0x0100);
}

int dram_init(void)
{
#if defined(CONFIG_CADENCE_DDR_CTRL)
	ddr_phy_init(RZN1_DDR3_SINGLE_BANK);

	/* Override DDR PHY related settings */
	ddr_350_374_async[351 - 350] = 0x001e0000;
	ddr_350_374_async[352 - 350] = 0x1e680000;
	ddr_350_374_async[353 - 350] = 0x02000020;
	ddr_350_374_async[354 - 350] = 0x02000200;
	ddr_350_374_async[355 - 350] = 0x00000c30;
	ddr_350_374_async[356 - 350] = 0x00009808;
	ddr_350_374_async[357 - 350] = 0x020a0706;
	ddr_350_374_async[372 - 350] = 0x01000000;

	/* DDR Controller is always in ASYNC mode */
	cdns_ddr_ctrl_init((void *)RZN1_DDR_BASE, 1,
			   ddr_00_87_async, ddr_350_374_async,
			   RZN1_V_DDR_BASE, CONFIG_SYS_SDRAM_SIZE);

	rzn1_ddr3_single_bank((void *)RZN1_DDR_BASE);
	cdns_ddr_set_diff_cs_delays((void *)RZN1_DDR_BASE, 2, 7, 2, 2);
	cdns_ddr_set_same_cs_delays((void *)RZN1_DDR_BASE, 0, 7, 0, 0);
	cdns_ddr_set_odt_times((void *)RZN1_DDR_BASE, 5, 6, 6, 0, 4);
	cdns_ddr_ctrl_start((void *)RZN1_DDR_BASE);

	ddr_phy_enable_wl();

#if defined(CONFIG_CADENCE_DDR_CTRL_ENABLE_ECC)
	/*
	 * Any read before a write will trigger an ECC un-correctable error,
	 * causing a data abort. However, this is also true for any read with a
	 * size less than the AXI bus width. So, the only sensible solution is
	 * to write to all of DDR now and take the hit...
	 */
	memset((void *)RZN1_V_DDR_BASE, 0xff, CONFIG_SYS_SDRAM_SIZE);

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

#if defined(RZN1_ENABLE_ETHERNET)
/* RIN Ether Accessory (Switch Control) regs */
#define MODCTRL				0x8
#define MT5PT_SWITCH_UPSTREAM_PORT	4

/*
 * RIN RGMII/RMII Converter and switch setup.
 * Called when DW ethernet determines the link speed
 */
int phy_adjust_link_notifier(struct phy_device *phy)
{
	struct eth_device *eth = phy->dev;

	if (eth->index == 0) {
		/* GMAC1 can only be connected to RGMII/RMII Converter 1 */
		rzn1_rgmii_rmii_conv_speed(0, phy->duplex, phy->speed);
	} else {
		int port;

		/* GMAC2 goes via the 5-port switch */
		/* All ports are enabled on the switch, but U-Boot only supports
		 * a single PHY attached to it. Since we have no idea which port
		 * the PHY is actually being used with, we update all ports.
		 */
		for (port = 0; port < 4; port++) {
			rzn1_rgmii_rmii_conv_speed(4 - port, phy->duplex, phy->speed);
			rzn1_switch_setup_port_speed(port, phy->speed, 1);
		}
	}

	return 0;
}

int board_eth_init(bd_t *bis)
{
	int ret = 0;
	int rst_gpio;

	rzn1_rin_init();

	/* Setup RGMII/RMII Converters */
	rzn1_rgmii_rmii_conv_setup(0, PHY_INTERFACE_MODE_RGMII, 0);
	rzn1_rgmii_rmii_conv_setup(1, PHY_INTERFACE_MODE_RGMII, 0);
	rzn1_rgmii_rmii_conv_setup(2, PHY_INTERFACE_MODE_RGMII, 0);
	rzn1_rgmii_rmii_conv_setup(3, PHY_INTERFACE_MODE_MII, 1);
	rzn1_rgmii_rmii_conv_setup(4, PHY_INTERFACE_MODE_MII, 1);

	/* RIN: Mode Control - GMAC2 on all Switch ports */
	rzn1_rin_prot_writel(0x13, MODCTRL);

	rzn1_mt5pt_switch_init();

	/* Upstream port is always 1Gbps */
	rzn1_switch_setup_port_speed(MT5PT_SWITCH_UPSTREAM_PORT, SPEED_1000, 1);

	rzn1_rin_reset_clks();

#ifdef CONFIG_APPLY_ETH_PHY_RESET_PULSE
	/* Reset the PHYs using a gpio */
	rst_gpio = rzn1_pin_to_gpio(94);
	gpio_request(rst_gpio, NULL);
	gpio_direction_output(rst_gpio, 0);
	mdelay(15);
	gpio_set_value(rst_gpio, 1);
#endif

	/* Note: GMAC1 is only used on EB board */
	/* Enable Ethernet GMAC1 (these regs are 0 indexed) */
	rzn1_clk_set_gate(RZN1_ACLK_MAC0_ID, 1);
	rzn1_clk_reset(RZN1_ACLK_MAC0_ID);
	if (designware_initialize(RZN1_GMAC0_BASE, PHY_INTERFACE_MODE_RGMII) >= 0)
		ret++;

	/* Enable Ethernet GMAC2 (code indexes start at 0).
	 * Uses a fixed 1Gbps link to the 5-port switch.
	 * The interface specified here is the PHY side, not 5-port switch side.
	 */
	rzn1_clk_set_gate(RZN1_ACLK_MAC1_ID, 1);
	rzn1_clk_reset(RZN1_ACLK_MAC1_ID);
	if (designware_initialize_fixed_link(RZN1_GMAC1_BASE, PHY_INTERFACE_MODE_MII, SPEED_1000) >= 0)
		ret++;

	return ret;
}

int board_phy_config(struct phy_device *phydev)
{
	if (phydev->drv->config)
		phydev->drv->config(phydev);

	return 0;
}
#endif	/* RZN1_ENABLE_ETHERNET */

void spl_board_init(void)
{
	arch_cpu_init();
	preloader_console_init();
	board_init();
	dram_init();

	rzn1_rin_init();

	/* Setup RGMII/RMII Converters */
	rzn1_rgmii_rmii_conv_setup(0, PHY_INTERFACE_MODE_RGMII, 0);
	rzn1_rgmii_rmii_conv_setup(1, PHY_INTERFACE_MODE_RGMII, 0);
	rzn1_rgmii_rmii_conv_setup(2, PHY_INTERFACE_MODE_RGMII, 0);
	rzn1_rgmii_rmii_conv_setup(3, PHY_INTERFACE_MODE_MII, 1);
	rzn1_rgmii_rmii_conv_setup(4, PHY_INTERFACE_MODE_MII, 1);

	/* RIN: Mode Control - GMAC2 on all Switch ports */
	rzn1_rin_prot_writel(0x13, MODCTRL);

	rzn1_mt5pt_switch_init();

	/* Upstream port is always 1Gbps */
	rzn1_switch_setup_port_speed(MT5PT_SWITCH_UPSTREAM_PORT, SPEED_1000, 1);

	rzn1_rin_reset_clks();

	rzn1_clk_set_gate(RZN1_ACLK_MAC1_ID, 1);
	rzn1_clk_reset(RZN1_ACLK_MAC1_ID);
}
