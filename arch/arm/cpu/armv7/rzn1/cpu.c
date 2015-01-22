/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <asm/io.h>
#include <common.h>
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

	/* 200MHz QSPI Ctrl clock. The driver divides to meet the requested
	 * SPI clock. This sysctrl divider can divide by any number, whereas
	 * the divider in the IP can only divide by even numbers. With this
	 * setting you can get SPI clocks of 100MHz, 50MHz, 25MHz, etc, down to
	 * the slowest speed of 6.25Mhz. So, you may need to change this to get
	 * a better or slower SPI clock.
	 */
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_QSPI0DIV, 5);

	/* 50MHz SDHCI clocks. */
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_SDIO0DIV, 20);
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_SDIO1DIV, 20);

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
	mmu_set_region_dcache_behaviour(RZN1_RAM2MB_ID_BASE,
					RZN1_RAM2MB_ID_SIZE,
					DCACHE_WRITETHROUGH);
	mmu_set_region_dcache_behaviour(RZN1_RAM2MB_SYS_BASE,
					RZN1_RAM2MB_SYS_SIZE,
					DCACHE_WRITETHROUGH);
	mmu_set_region_dcache_behaviour(RZN1_V_QSPI_BASE,
					RZN1_V_QSPI_SIZE,
					DCACHE_WRITETHROUGH);
}
#endif

#if defined(CONFIG_ARMV7_NONSEC) || defined(CONFIG_ARMV7_VIRT)
/* Setting the address at which secondary core starts from */
void smp_set_core_boot_addr(unsigned long addr, int corenr)
{
	writel(addr, CONFIG_SMP_PEN_ADDR);

	/* Write a canary so that Linux knows which PEN address to use */
	writel(0x525a4e31, CONFIG_SMP_PEN2_ADDR + 4);
}
#endif
