/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	BSD-2-Clause
 */
#include <asm/io.h>
#include <common.h>
#include <usb.h>
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-utils.h"
#include "renesas/rzn1-clocks.h"
#include "renesas/rzn1-sysctrl.h"

void lowlevel_init(void)
{
}

void rzn1_sysctrl_div(u32 reg, u32 div)
{
	/* Wait for busy bit to be cleared */
	while (sysctrl_readl(reg) & (1 << 31))
		;

	/* New divider setting */
	sysctrl_writel((1 << 31) | div, reg);

	/* Wait for busy bit to be cleared */
	while (sysctrl_readl(reg) & (1 << 31))
		;
}

void rzn1_setup_pinmux(void);

int arch_cpu_init(void)
{
	/*
	 * Setup clocks to IP blocks, all are divided down from a 1GHz PLL.
	 */

	/* 20MHz UART clock. This can generate 114942 baud, i.e 0.22% error. */
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_BASICS_UARTDIV, 50);

	/* 83.33MHz NAND Flash Ctrl clock, fastest as NAND is an async i/f. */
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_NFLASHDIV, 12);

	/* 250MHz QSPI Ctrl clock. The driver divides to meet the requested
	 * SPI clock. This sysctrl divider can divide by any number, whereas
	 * the divider in the IP can only divide by even numbers. However,
	 * Cadence recommend the IP divider is set to a minimum of 4 and the
	 * SPI clock maximum is 62.5MHz. With this PLL setting you can get SPI
	 * clocks of 62.5MHz, 31.25MHz, etc.
	 */
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_QSPI0DIV, 4);
#if defined(CONFIG_RZN1S_USE_QSPI1)
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_QSPI1DIV, 4);
#endif

	/* 100MHz I2C clock */
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_BASICS_I2CDIV, 10);

	/* 50MHz SDHCI clocks. */
	#define SDHC_CLK_MHZ 50
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_SDIO0DIV, 1000/SDHC_CLK_MHZ);
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_SDIO1DIV, 1000/SDHC_CLK_MHZ);
	sysctrl_writel(SDHC_CLK_MHZ, RZN1_SYSCTRL_REG_CFG_SDIO0);
	sysctrl_writel(SDHC_CLK_MHZ, RZN1_SYSCTRL_REG_CFG_SDIO1);

	/* Enable pinmux clocks and FlexWAY connection for UART */
	rzn1_clk_set_gate(RZN1_CLK_25MHZ_UDL1_ID, 1);
	rzn1_clk_set_gate(RZN1_PCLK_GPIO0_ID, 1);
	rzn1_clk_set_gate(RZN1_PCLK_GPIO1_ID, 1);

	/* Enable UART clock and FlexWAY connection */
#if (CONFIG_CONS_INDEX == 1)
	rzn1_clk_set_gate(RZN1_PCLK_UART0_ID, 1);
	rzn1_clk_set_gate(RZN1_SCLK_UART0_ID, 1);
#elif (CONFIG_CONS_INDEX == 2)
	rzn1_clk_set_gate(RZN1_PCLK_UART1_ID, 1);
	rzn1_clk_set_gate(RZN1_SCLK_UART1_ID, 1);
#elif (CONFIG_CONS_INDEX == 3)
	rzn1_clk_set_gate(RZN1_PCLK_UART2_ID, 1);
	rzn1_clk_set_gate(RZN1_SCLK_UART2_ID, 1);
#endif
	rzn1_setup_pinmux();

	return 0;
}

/* Configure clocks for the USB blocks and reset IP */
int rzn1_usb_init(int index, enum usb_init_type init)
{
#define USBFUNC_EPCTR		(RZN1_USB_DEV_BASE + 0x1000 + 0x10)

	u32 val;

	/* Enable USB clocks */
	rzn1_clk_set_gate(RZN1_CLK_USBPM_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_USBF_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_USBH_ID, 1);

	/*
	 * Enable USB on port 1 as the Linux kernel doesn't deal with
	 * these irregular register bits
	 */
	val = sysctrl_readl(RZN1_SYSCTRL_REG_CFG_USB);
	val |= (1 << RZN1_SYSCTRL_REG_CFG_USB_DIRPD);
	val |= (1 << RZN1_SYSCTRL_REG_CFG_USB_FRCLK48MOD);
	if (index == 0 && init == USB_INIT_HOST)
		val |= (1 << RZN1_SYSCTRL_REG_CFG_USB_H2MODE);
	sysctrl_writel(val, RZN1_SYSCTRL_REG_CFG_USB);

	/* Hold USBF in reset */
	writel(5, USBFUNC_EPCTR);
	udelay(100);

	/* Power up USB PLL */
	val = sysctrl_readl(RZN1_SYSCTRL_REG_CFG_USB);
	val &= ~(1 << RZN1_SYSCTRL_REG_CFG_USB_DIRPD);
	sysctrl_writel(val, RZN1_SYSCTRL_REG_CFG_USB);
	udelay(100000);

	/* Release USBF resets */
	writel(0, USBFUNC_EPCTR);

	/* Wait for USB PLL lock */
	do {
		val = sysctrl_readl(RZN1_SYSCTRL_REG_USBSTAT);
	} while (!(val & (1 << RZN1_SYSCTRL_REG_USBSTAT_PLL_LOCK)));

	return 0;
}

void reset_cpu(ulong addr)
{
	sysctrl_writel((1 << RZN1_SYSCTRL_REG_RSTEN_SWRST_EN) | (1 << RZN1_SYSCTRL_REG_RSTEN_MRESET_EN),
			RZN1_SYSCTRL_REG_RSTEN);
	sysctrl_writel((1 << RZN1_SYSCTRL_REG_RSTCTRL_SWRST_REQ), RZN1_SYSCTRL_REG_RSTCTRL);

	while (1)
		;
}

#ifndef CONFIG_SYS_DCACHE_OFF
void enable_caches(void)
{
	/* Enable D-cache. I-cache is already enabled in start.S */
	dcache_enable();

	/* Set on-chip SRAMs & QSPI as cached, but set them to write-through */
	mmu_set_region_dcache_behaviour(RZN1_SRAM_ID_BASE,
					RZN1_SRAM_ID_SIZE,
					DCACHE_WRITETHROUGH);
	mmu_set_region_dcache_behaviour(RZN1_SRAM_SYS_BASE,
					RZN1_SRAM_SYS_SIZE,
					DCACHE_WRITETHROUGH);
	mmu_set_region_dcache_behaviour(RZN1_V_QSPI_BASE,
					RZN1_V_QSPI_SIZE,
					DCACHE_WRITETHROUGH);
}
#endif

#if defined(CONFIG_ARMV7_NONSEC) || defined(CONFIG_ARMV7_VIRT)
/* This is the location the smp_pen code will loop onto to find it's
 * boot address. The address of /this/ variable is passed down to the
 * kernel via the /chosen/rzn1,bootaddr property for the kernel to
 * take over the parked core */
u32 smp_secondary_bootaddr[2] = { 0, 0x525a4e31 };

/* Setting the address at which secondary core starts from */
void smp_set_core_boot_addr(unsigned long addr, int corenr)
{
	/* BootROM holding pen */
	writel(addr, RZN1_SYSTEM_CTRL_BASE + RZN1_SYSCTRL_REG_BOOTADDR);

	/* U-Boot holding pen */
	smp_secondary_bootaddr[0] = addr;
}

#endif

#if defined(CONFIG_OF_LIBFDT) && defined(CONFIG_OF_BOARD_SETUP)
#include <fdt_support.h>

/* this allows rzn1 boards to override and add properties */
void __weak __ft_board_setup(void *blob, bd_t *bd) {}

void ft_board_setup(void *blob, bd_t *bd)
{
#if defined(CONFIG_ARMV7_NONSEC) || defined(CONFIG_ARMV7_VIRT)
#if !defined(CONFIG_SPL_BUILD)
	if (getenv_yesno("boot_hyp") != 1 && getenv_yesno("boot_nonsec") != 1)
		return;
#endif

	u32 bootaddr = htonl((u32)smp_secondary_bootaddr);

	__ft_board_setup(blob, bd);

	fdt_find_and_setprop(blob, "/chosen", "rzn1,bootaddr",
			     &bootaddr, sizeof(bootaddr), 1);
#endif
}
#endif

/* Enable the Cortex M3 clock and it starts executing, IVT is at 0x04000000 */
static int do_rzn1_start_cm3(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	sysctrl_writel(0x3, RZN1_SYSCTRL_REG_PWRCTRL_CM3);

	return 0;
}

U_BOOT_CMD(
	rzn1_start_cm3, 1, 1, do_rzn1_start_cm3,
	"Start Cortex M3 core", ""
);
