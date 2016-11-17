/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	BSD-2-Clause
 */
#include <common.h>
#include <cadence_ddr_ctrl.h>
#include <asm/armv7.h>
#include <asm/io.h>
#include <usb.h>
#include "ipcm.h"
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
	/* 500MHz clock input to the CPU clock divider */
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_CA7DIV, 1);

	/* Setup clocks to IP blocks, all are divided down from a 1GHz PLL */

	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_PG0_UARTDIV, 1000000000 / CONFIG_SYS_NS16550_CLK);
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_PG1_PR2DIV, 1000000000 / CONFIG_SYS_NS16550_CLK);

	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_NFLASHDIV, 1000000000 / CONFIG_SYS_NAND_CLOCK);

	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_QSPI0DIV, 1000000000 / CONFIG_CQSPI_REF_CLK);

#if defined (CONFIG_ARCH_RZN1S)
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_QSPI1DIV, 1000000000 / CONFIG_CQSPI_REF_CLK);
#endif

	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_PG0_I2CDIV, 1000 / IC_CLK);

	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_SDIO0DIV, 1000/SDHC_CLK_MHZ);
	sysctrl_writel(SDHC_CLK_MHZ, RZN1_SYSCTRL_REG_CFG_SDIO0);

#if !defined (CONFIG_ARCH_RZN1L)
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_SDIO1DIV, 1000/SDHC_CLK_MHZ);
	sysctrl_writel(SDHC_CLK_MHZ, RZN1_SYSCTRL_REG_CFG_SDIO1);
#endif

	/* Enable pinmux clocks and FlexWAY connection for UART */
	rzn1_clk_set_gate(RZN1_HCLK_PINCONFIG_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_GPIO0_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_GPIO1_ID, 1);

	/* Enable UART clock and FlexWAY connection */
	rzn1_clk_set_gate(RZN1_HCLK_UART0_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_UART0_ID, 1);

	rzn1_setup_pinmux();

	return 0;
}

/* Configure clocks for the USB blocks and reset IP */
int rzn1_usb_init(int index, enum usb_init_type init)
{
#define USBFUNC_EPCTR		(RZN1_USB_DEV_BASE + 0x1000 + 0x10)

	u32 val;

	/* Enable USB clocks */
	rzn1_clk_set_gate(RZN1_HCLK_USBPM_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_USBF_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_USBH_ID, 1);

	/* USB Host clocks */
	rzn1_clk_set_gate(RZN1_CLK_PCI_USB_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_48MHZ_PG_F_ID, 1);

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
#if !defined (CONFIG_ARCH_RZN1L)
void post_mmu_setup(void)
{
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
	if (!is_rzn1d()) {
		mmu_set_region_dcache_behaviour(RZN1_SRAM4MB_BASE,
					RZN1_SRAM4MB_SIZE,
					DCACHE_WRITETHROUGH);
		mmu_set_region_dcache_behaviour(RZN1_V_QSPI1_BASE,
					RZN1_V_QSPI1_SIZE,
					DCACHE_WRITETHROUGH);
	}
}
#endif

void enable_caches(void)
{
	/* Enable D-cache. I-cache is already enabled in start.S */
	dcache_enable();
}
#endif

#if defined(CONFIG_ARMV7_NONSEC) || defined(CONFIG_ARMV7_VIRT)
#include <asm/secure.h>
/* This is the location the smp_pen code will loop onto to find it's
 * boot address. The address of /this/ variable is passed down to the
 * kernel via the /chosen/rzn1,bootaddr property for the kernel to
 * take over the parked core */
u32 smp_secondary_bootaddr[10] __secure_data = { 0, 0x525a4e31 };

/* Setting the address at which secondary core starts from */
void smp_set_core_boot_addr(unsigned long addr, int corenr)
{
	debug("%s: writing %08lx to SYSCTRL reg and 2nd bootaddr (%08x)\n",
		__func__, addr, (u32)smp_secondary_bootaddr);

	/*
	 * To protect against spurious wake up events, the smp_waitloop will go
	 * back into wfi/wfe if the jump address is the the address of where it
	 * got the jump address. In our case, the jump address is stored in
	 * smp_secondary_bootaddr[0], so that's why we set it here.
	 */
	smp_secondary_bootaddr[0] = addr;
	flush_dcache_all();

	/* BootROM holding pen */
	writel(addr, RZN1_SYSTEM_CTRL_BASE + RZN1_SYSCTRL_REG_BOOTADDR);
}

#endif

#if defined(CONFIG_OF_LIBFDT) && defined(CONFIG_OF_BOARD_SETUP)
#include <fdt_support.h>

/* this allows rzn1 boards to override and add properties */
int __weak __ft_board_setup(void *blob, bd_t *bd)
{
	return 0;
}

/* This function updates the Device Tree that is passed to Linux for starting
 * up core 1 (SMP). Note that full U-Boot may or may not switch to NONSEC, or
 * NONSEC+HYP, depending on env variables.
 */
int ft_board_setup(void *blob, bd_t *bd)
{
#if defined(CONFIG_ARMV7_NONSEC)
#if !defined(CONFIG_SPL_BUILD)
	if (!armv7_boot_nonsec())
		return 0;
#endif

	u32 bootaddr = htonl((u32)smp_secondary_bootaddr);

	__ft_board_setup(blob, bd);

	fdt_find_and_setprop(blob, "/chosen", "rzn1,bootaddr",
			     &bootaddr, sizeof(bootaddr), 1);
#endif
	return 0;
}
#endif

#define IPC_TX_MBOX		1
#define IPC_RX_MBOX		0

struct ipc_msg {
	uint32_t	msg_type;	/* Number to allow different msgs */
	uint32_t	wait_rsp;
	uint32_t	env_ptr;
	uint32_t	pad[4];
};

void rzn1_ddr_ctrl_init(const u32 *reg0, const u32 *reg350, u32 ddr_size)
{
	u32 ddr_start_addr = 0;

	/*
	 * On ES1.0 devices, the DDR start address that the DDR Controller sees
	 * is the physical address of the DDR. However, later devices changed it
	 * to be 0 in order to fix an issue with DDR out-of-range detection.
	 */
	if (sysctrl_readl(RZN1_SYSCTRL_REG_VERSION) == 0x10)
		ddr_start_addr = RZN1_V_DDR_BASE;

	/* DDR Controller is always in ASYNC mode */
	cdns_ddr_ctrl_init((void *)RZN1_DDR_BASE, 1,  reg0, reg350,
			   ddr_start_addr, ddr_size);
}

/* Enable the Cortex M3 clock and it starts executing, IVT is at 0x04000000 */
static int do_rzn1_start_cm3(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	struct ipc_msg msg = { 0 };

	msg.msg_type = 1;
	msg.wait_rsp = 0;
	msg.env_ptr = RZN1_V_QSPI_BASE + CONFIG_ENV_OFFSET;

	/* Check the CM3 reset vector has something there */
	if (readl(RZN1_SRAM_ID_BASE + 4) == 0)
		printf("Warning: CM3 reset vector is 0!\n");

	if (argc >= 2) {
		msg.wait_rsp = simple_strtoul(argv[1], NULL, 10);
		if (!msg.wait_rsp)
			return -1;
	}
	if (argc >= 3) {
		msg.env_ptr = simple_strtoul(argv[2], NULL, 16);
		if (!msg.env_ptr)
			return -1;
	}

	ipc_init(RZN1_MAILBOX_BASE);
	ipc_setup_1to1(IPC_TX_MBOX, IPC_RX_MBOX);
	ipc_setup_1to1(IPC_RX_MBOX, IPC_TX_MBOX);

	ipc_send(IPC_TX_MBOX, (void *)&msg);

	/* Reset then enable the Cortex M3 clock */
	sysctrl_writel(0x5, RZN1_SYSCTRL_REG_PWRCTRL_CM3);
	udelay(10);
	sysctrl_writel(0x3, RZN1_SYSCTRL_REG_PWRCTRL_CM3);

	/* Wait for the CM3 to send a msg back */
	if (msg.wait_rsp)
		ipc_recv_all(IPC_RX_MBOX, (void *)&msg);

	return 0;
}

U_BOOT_CMD(
	rzn1_start_cm3, 3, 0, do_rzn1_start_cm3,
	"Start Cortex M3 core",
	   "<wait> 0x<address>\n"
	   "    - wait = 0|1: Wait for cm3 to ack using IPCM register\n"
	   "    - Pass the address specified via the IPCM register\n"
);
