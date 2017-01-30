/*
 * Renesas RZ/N1S-324 Demo Board, can be used with RZ/N1 Extension Board
 *
 * (C) Copyright 2017 Renesas Electronics Europe Limited
 *
 * SPDX-License-Identifier:	GPL-2.0+
 *
 */
/*
 * WARNING! All hardware information (device and board) indexes start at 1,
 * whereas all software indexes start at 0. Everything in this file refers
 * to the software indexes.
 */

#include <common.h>
#include <miiphy.h>
#include <netdev.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <dm.h>
#include <fpga.h>
#include <lattice.h>
#include <led.h>
#include <linux/sizes.h>
#include <malloc.h>
#include <sdhci.h>
#include <spl.h>
#include <usb.h>
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-sysctrl.h"
#include "renesas/rzn1-utils.h"
#include "renesas/rzn1-clocks.h"
#include "renesas/pinctrl-rzn1.h"
#define USE_DEFAULT_PINMUX
#include "rzn1-board-pinmux.h"
#include "cadence_ddr_ctrl.h"

DECLARE_GLOBAL_DATA_PTR;

int board_early_init_f(void)
{
	return 0;
}

/* Called early during device initialisation */
void rzn1_setup_pinmux(void)
{
	/* Set all pin mux and drive stength to defaults for this board */
	rzn1_board_pinmux(-1);

	/*
	 * This is special 'virtual' pins for the MDIO multiplexing.
	 * The default sets MDIO1 control to the 5-port Switch, but U-Boot
	 * doesn't have a driver for this, hence MDIO1 is controlled by GMAC1
	 */
	rzn1_pinmux_set(RZN1_MUX_MDIO(RZN1_MDIO_BUS1, MDIO_MUX_MAC1));
}

#if defined(RZN1_ENABLE_USBF) && !defined(CONFIG_SPL_BUILD)
/* Configure board specific clocks for the USB blocks */
int board_usb_init(int index, enum usb_init_type init)
{
	/* Configure device clocks, etc */
	return rzn1_usb_init(index, init);
}
#endif

static int set_led(const char *name, int val)
{
	struct udevice *dev;
	int ret;

	ret = led_get_by_label(name, &dev);
	if (ret)
		return ret;

	return led_set_on(dev, val);
}

int rzn1_ctrl(void)
{
#if defined(RZN1_ENABLE_I2C)
	/*
	 * Switch I2C master for the EEPROM from the FTDI device to the RZ/N1.
	 * The order is important as we want to avoid two masters connected at
	 * the same time.
	 * NOTE: ctrl0 is marked as ACTIVE_LOW in the dts
	 */
	/* Pretend the ctrl pins are leds. It works... */
	set_led("ctrl1", 0);
	set_led("ctrl2", 0);
	set_led("ctrl0", 0);
#endif

	return 0;
}

#if defined(CONFIG_FPGA) && !defined(CONFIG_SPL_BUILD)
#define CPLD_TDO_PIN	88
#define CPLD_TMS_PIN	89
#define CPLD_TDI_PIN	90
#define CPLD_TCK_PIN	91
static void rzn1_jtag_init(void)
{
	/* Uses PMOD3 pins as GPIOs - that's the default setting.
	 * PMOD3 pin   GPIO-No   JTAG connector
	 * CN4 pin 1     88      TDO on CN3 pin 6
	 * CN4 pin 2     89      TMS on CN3 pin 5
	 * CN4 pin 3     90      TDI on CN3 pin 4
	 * CN4 pin 4     91      TCK on CN3 pin 3
	 */
	nondm_gpio_request(CPLD_TDO_PIN, "tdo");
	nondm_gpio_direction_input(CPLD_TDO_PIN);
	nondm_gpio_request(CPLD_TMS_PIN, "tms");
	nondm_gpio_direction_output(CPLD_TMS_PIN, 1);
	nondm_gpio_request(CPLD_TDI_PIN, "tdi");
	nondm_gpio_direction_output(CPLD_TDI_PIN, 0);
	nondm_gpio_request(CPLD_TCK_PIN, "tck");
	nondm_gpio_direction_output(CPLD_TCK_PIN, 0);
}

static void rzn1_fpga_jtag_set_tdi(int value)
{
	nondm_gpio_set_value(CPLD_TDI_PIN, value);
}

static void rzn1_fpga_jtag_set_tms(int value)
{
	nondm_gpio_set_value(CPLD_TMS_PIN, value);
}

static void rzn1_fpga_jtag_set_tck(int value)
{
	nondm_gpio_set_value(CPLD_TCK_PIN, value);
}

static int rzn1_fpga_jtag_get_tdo(void)
{
	return nondm_gpio_get_value(CPLD_TDO_PIN);
}

lattice_board_specific_func rzn1_fpga_fns = {
	rzn1_jtag_init,
	rzn1_fpga_jtag_set_tdi,
	rzn1_fpga_jtag_set_tms,
	rzn1_fpga_jtag_set_tck,
	rzn1_fpga_jtag_get_tdo
};

Lattice_desc rzn1_fpga = {
	Lattice_XP2,		/* XO2 behaves the same as XP2 */
	lattice_jtag_mode,
	65535,
	(void *) &rzn1_fpga_fns,
	NULL,
	0,
	"LCMXO2-256HC"
};

int rzn1_fpga_init(void)
{
	fpga_init();
	fpga_add(fpga_lattice, &rzn1_fpga);

	return 0;
}
#endif	/* defined(CONFIG_FPGA) */

int board_init(void)
{
#if defined(RZN1_ENABLE_QSPI)
	/* Enable QSPI */
	rzn1_clk_set_gate(RZN1_CLK_QSPI0_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_QSPI0_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_QSPI1_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_QSPI1_ID, 1);
#endif

#if defined(RZN1_ENABLE_I2C) && !defined(CONFIG_SPL_BUILD)
	rzn1_clk_set_gate(RZN1_HCLK_I2C1_ID, 1);
#endif

#if defined(RZN1_ENABLE_SDHC) && !defined(CONFIG_SPL_BUILD)
	/* Enable SDHC0 */
	rzn1_clk_set_gate(RZN1_CLK_SDIO0_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_SDIO0_ID, 1);
	rzn1_clk_reset(RZN1_HCLK_SDIO0_ID);
#endif

#if defined(RZN1_ENABLE_GPIO)
	/* Enable GPIO clock */
	rzn1_clk_set_gate(RZN1_HCLK_GPIO0_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_GPIO1_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_GPIO2_ID, 1);
#endif

#if defined(RZN1_ENABLE_USBF) && !defined(CONFIG_SPL_BUILD)
		board_usb_init(0, USB_INIT_DEVICE);
#endif

#if defined(CONFIG_ARMV7_NONSEC) && defined(CONFIG_ARMV7_NONSEC_AT_BOOT)
	/* Change to non-secure mode now */
	armv7_init_nonsec();
#endif

#if defined(CONFIG_FPGA) && !defined(CONFIG_SPL_BUILD)
	rzn1_fpga_init();
#endif

	return 0;
}

int board_late_init(void)
{
#if defined(RZN1_ENABLE_I2C) && !defined(CONFIG_SPL_BUILD)
	/*
	 * Do any I2C here, not board_init(). When board_init() runs, U-Boot has
	 * not setup the UART properly so any I2C error that results in an error
	 * message being printed out will result in a data abort.
	 */
	rzn1_ctrl();
#endif

	return 0;
}


int dram_init(void)
{
	/* No DDR possible on this device */
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
#ifdef CONFIG_DM_ETH
	struct udevice *dev = phy->dev;
	struct eth_pdata *pdata = dev_get_platdata(dev);
	int gmac0 = pdata->iobase == RZN1_GMAC0_BASE ? 1 : 0;
#else
	struct eth_device *eth = phy->dev;
	int gmac0 = eth->index == 0 ? 1 : 0;
#endif

	if (gmac0) {
		/* GMAC0 can only be connected to RGMII/RMII Converter 0 */
		rzn1_rgmii_rmii_conv_speed(0, phy->duplex, phy->speed);
	} else {
		int port;

		/* GMAC1 goes via the 5-port switch */
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

static int rzn1_board_eth_init(void)
{
	int ret = 0;

	rzn1_rin_init();

	/* Setup RGMII/RMII Converters */
	rzn1_rgmii_rmii_conv_setup(0, PHY_INTERFACE_MODE_RGMII_ID, 0);
	rzn1_rgmii_rmii_conv_setup(1, PHY_INTERFACE_MODE_RGMII_ID, 0);
	rzn1_rgmii_rmii_conv_setup(2, PHY_INTERFACE_MODE_RGMII_ID, 0);
	rzn1_rgmii_rmii_conv_setup(3, PHY_INTERFACE_MODE_MII, 1);
	rzn1_rgmii_rmii_conv_setup(4, PHY_INTERFACE_MODE_MII, 1);

	/* RIN: Mode Control - GMAC1 on all Switch ports */
	rzn1_rin_prot_writel(0x13, MODCTRL);

	rzn1_mt5pt_switch_init();

	/* Upstream port is always 1Gbps */
	rzn1_switch_setup_port_speed(MT5PT_SWITCH_UPSTREAM_PORT, SPEED_1000, 1);

	rzn1_rin_reset_clks();

#ifdef RZN1_APPLY_ETH_PHY_RESET_PULSE
	struct gpio_desc reset_gpio = {};
	int node;

	/* The PHY reset pin is in the 5-port switch DT node because the PHYs
	 * are connected to the switch and not to the GMAC. */
	node = fdt_node_offset_by_compatible(gd->fdt_blob, 0, "mtip,5pt_switch");
	if (node < 0)
		return node;

	ret = gpio_request_by_name_nodev(gd->fdt_blob, node, "phy-reset-gpios", 0,
				 &reset_gpio, GPIOD_IS_OUT);
	if (ret)
		return ret;

	/* reset the phy */
	ret = dm_gpio_set_value(&reset_gpio, 0);
	if (ret)
		return ret;

	mdelay(15);

	ret = dm_gpio_set_value(&reset_gpio, 1);
	if (ret)
		return ret;
#endif

	/* Enable Ethernet GMAC0 (only used on EB board) */
	rzn1_clk_set_gate(RZN1_HCLK_GMAC0_ID, 1);
	rzn1_clk_reset(RZN1_HCLK_GMAC0_ID);

	/* Enable Ethernet GMAC1 */
	rzn1_clk_set_gate(RZN1_HCLK_GMAC1_ID, 1);
	rzn1_clk_reset(RZN1_HCLK_GMAC1_ID);

	return ret;
}

int board_eth_init(bd_t *bis)
{
	int ret;
	u32 if_type;

	ret = rzn1_board_eth_init();
	if (ret)
		return ret;

	/* Enable Ethernet GMAC0 (only used on EB board) */
	if (designware_initialize(RZN1_GMAC0_BASE, PHY_INTERFACE_MODE_RGMII_ID) >= 0)
		ret++;

	/* Work out which PHY interface we are using based in the PHY address */
	if ((CONFIG_PHY1_ADDR == 4) || (CONFIG_PHY1_ADDR == 5))
		if_type = PHY_INTERFACE_MODE_MII;
	else
		if_type = PHY_INTERFACE_MODE_RGMII_ID;

	/* Enable Ethernet GMAC1.
	 * Uses a fixed 1Gbps link to the 5-port switch.
	 * The interface specified here is the PHY side, not 5-port switch side.
	 */
	if (designware_initialize_fixed_link(RZN1_GMAC1_BASE, if_type, SPEED_1000) >= 0)
		ret++;

	return ret;
}

/* Re-use Marvell function */
void m88e1518_phy_writebits(struct phy_device *phydev,
		   u8 reg_num, u16 offset, u16 len, u16 data);

int board_phy_config(struct phy_device *phydev)
{
	if (phydev->drv->config)
		phydev->drv->config(phydev);

	/*
	 * Board Design Note:
	 * Depending on switch settings and pin multiplexing, the 5-Port Switch
	 * may use the LED[0] from the PHYs as a link up/down status signal, so
	 * we program the PHYs to output this.
	 * Note: This only changes the PHYs that are actually used by U-Boot.
	 *
	 * The KSZ8041 PHY LED[0] signal must be inverted by the R-In Engine
	 * using the "Ethernet PHY Link Mode" reg, see board_init() above.
	 * If the Switch Link Status signal is disabled by hardware, the 5-Port
	 * Switch will think the link is permanently down.
	 */
#define MARVELL_88E1512		0x1410dd4
#define MII_MARVELL_PHY_PAGE	22
	if (phydev->phy_id == MARVELL_88E1512) {
		phy_write(phydev, MDIO_DEVAD_NONE, MII_MARVELL_PHY_PAGE, 3);

		/* LED Func Control: LED[0]: link up = On, link down = Off */
		m88e1518_phy_writebits(phydev, 16, 0, 4, 0);

		/* LED[2] is used as an active low interrupt */
		m88e1518_phy_writebits(phydev, 18, 7, 1, 1);

		phy_write(phydev, MDIO_DEVAD_NONE, MII_MARVELL_PHY_PAGE, 0);
	}

#define MICREL_KSZ8041		0x221513
	if (phydev->phy_id == MICREL_KSZ8041) {
		/* LED Mode: link up = drive low, link down = drive high */
		m88e1518_phy_writebits(phydev, 0x1e, 14, 2, 1);
	}

#define PHY_LINK_MODE		0x14		/* Ethernet PHY Link Mode */
	/* R-IN Engine: Invert all of the 5-Port Switch Link Status signals */
	rzn1_rin_prot_writel(0x37f, PHY_LINK_MODE);

	return 0;
}
#endif	/* RZN1_ENABLE_ETHERNET */

#if defined(CONFIG_SPL_BOARD_INIT)
void spl_board_init(void)
{
	arch_cpu_init();
	preloader_console_init();
	board_init();
	dram_init();

#if defined(RZN1_ENABLE_ETHERNET)
	rzn1_board_eth_init();
#endif
}
#endif
