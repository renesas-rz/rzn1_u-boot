/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	BSD-2-Clause
 */
#include <common.h>
#include <cadence_ddr_ctrl.h>
#include <asm/armv7.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <usb.h>
#include "ipcm.h"
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-utils.h"
#include "renesas/rzn1-clocks.h"
#include "renesas/rzn1-sysctrl.h"

DECLARE_GLOBAL_DATA_PTR;

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

#define USBF_EPCTR		(RZN1_USB_DEV_BASE + 0x1000 + 0x10)
#define USBF_EPCTR_EPC_RST	BIT(0)
#define USBF_EPCTR_PLL_RST	BIT(2)
#define USBF_EPCTR_DIRPD	BIT(12)

#define USBH_USBCTR		(RZN1_USB_HOST_BASE + 0x10000 + 0x834)
#define USBH_USBCTR_USBH_RST	BIT(0)
#define USBH_USBCTR_PCICLK_MASK	BIT(1)
#define USBH_USBCTR_PLL_RST	BIT(2)
#define USBH_USBCTR_DIRPD	BIT(9)

static int rzn1_usb_pll_locked(void)
{
	return (sysctrl_readl(RZN1_SYSCTRL_REG_USBSTAT) &
		BIT(RZN1_SYSCTRL_REG_USBSTAT_PLL_LOCK));
}

static void rzn1_enable_usb_pll(int h2mode)
{
	u32 sysc_cfg_usb = RZN1_SYSTEM_CTRL_BASE + RZN1_SYSCTRL_REG_CFG_USB;
	u32 val;

	/* If we have the correct mode and PLL is locked, nothing to do */
	val = sysctrl_readl(RZN1_SYSCTRL_REG_CFG_USB);
	val >>= RZN1_SYSCTRL_REG_CFG_USB_H2MODE;
	if (((val & 1) == h2mode) && rzn1_usb_pll_locked())
		return;

	/* Disable UART user of the USB clock */
	if (CONFIG_SYS_NS16550_CLK == 48000000)
		rzn1_clk_set_gate(RZN1_CLK_USBUART0_ID, 0);

	/* Enable USB clocks, otherwise we can't access the USB registers */
	rzn1_clk_set_gate(RZN1_HCLK_USBF_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_USBH_ID, 1);

	/* Hold USBF and USBH in reset */
	writel(USBH_USBCTR_USBH_RST | USBH_USBCTR_PCICLK_MASK, USBH_USBCTR);
	writel(USBF_EPCTR_EPC_RST, USBF_EPCTR);
	/* Hold USBPLL in reset */
	setbits_le32(USBH_USBCTR, USBH_USBCTR_PLL_RST);
	udelay(2);

	/* Power down USB PLL, setting any DIRPD bit will do this */
	setbits_le32(USBH_USBCTR, USBH_USBCTR_DIRPD);

	/* Stop USB suspend from powering down the USB PLL */
	/* Note: have to update these bits at the same time */
	val = sysctrl_readl(RZN1_SYSCTRL_REG_CFG_USB);
	val |= BIT(RZN1_SYSCTRL_REG_CFG_USB_FRCLK48MOD);
	if (h2mode)
		val |= BIT(RZN1_SYSCTRL_REG_CFG_USB_H2MODE);
	else
		val &= ~BIT(RZN1_SYSCTRL_REG_CFG_USB_H2MODE);
	sysctrl_writel(val, RZN1_SYSCTRL_REG_CFG_USB);

	/* Power up USB PLL, all DIRPD bits need to be cleared */
	clrbits_le32(sysc_cfg_usb, BIT(RZN1_SYSCTRL_REG_CFG_USB_DIRPD));
	clrbits_le32(USBF_EPCTR, USBF_EPCTR_DIRPD);
	clrbits_le32(USBH_USBCTR, USBH_USBCTR_DIRPD);
	udelay(1000);

	/* Release USBPLL reset, either PLL_RST bit will do this */
	clrbits_le32(USBH_USBCTR, USBH_USBCTR_PLL_RST);

	/* Turn off USB Host/Func clocks */
	rzn1_clk_set_gate(RZN1_HCLK_USBF_ID, 0);
	rzn1_clk_set_gate(RZN1_HCLK_USBH_ID, 0);

	while (!rzn1_usb_pll_locked())
		;

	/* Enable UART user of the USB clock */
	if (CONFIG_SYS_NS16550_CLK == 48000000)
		rzn1_clk_set_gate(RZN1_CLK_USBUART0_ID, 1);
}

#if defined(RZN1_DONT_TURN_OFF_CLOCKS)
static void rzn1_start_usb_compat(void)
{
	/*
	 * Technically, we only need to enable the USB PLL if the UART input
	 * clock is set to 48MHz, or if using USB in U-Boot.
	 * However, older RZ/N1 Linux kernels need the PLL on and the USB Func
	 * taken out of reset.
	 */
	rzn1_enable_usb_pll(0);

	/* Enable USBF bus clock */
	rzn1_clk_set_gate(RZN1_HCLK_USBF_ID, 1);

	/* Release USBF resets */
#define USBFUNC_EPCTR		(RZN1_USB_DEV_BASE + 0x1000 + 0x10)
	writel(0, USBFUNC_EPCTR);
}
#endif

int arch_cpu_init(void)
{
#if !defined(RZN1_DONT_TURN_OFF_CLOCKS)
	int i;

	/* Turn off all clocks to save power */
	for (i = 0; i <= RZN1_CLK_RTOS_MDC_ID; i++)
		rzn1_clk_set_gate(i, 0);
#endif

	/* 500MHz clock input to the CPU clock divider */
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_CA7DIV, 1);

	/* Setup clocks to IP blocks, all are divided down from a 1GHz PLL */

	/* UART 48MHz is special, it doesn't come from the UART PLL divider */
	if (CONFIG_SYS_NS16550_CLK == 48000000) {
		u32 val;

		rzn1_enable_usb_pll(0);

		/* UARTs 0..2 */
		val = sysctrl_readl(RZN1_SYSCTRL_REG_PWRCTRL_PG0_0);
		val |= BIT(RZN1_SYSCTRL_REG_PWRCTRL_PG0_0_UARTCLKSEL);
		sysctrl_writel(val, RZN1_SYSCTRL_REG_PWRCTRL_PG0_0);

		/* UARTs 3..7 */
		val = sysctrl_readl(RZN1_SYSCTRL_REG_PWRCTRL_PG1_PR2);
		val |= BIT(RZN1_SYSCTRL_REG_PWRCTRL_PG1_PR2_UARTCLKSEL);
		sysctrl_writel(val, RZN1_SYSCTRL_REG_PWRCTRL_PG1_PR2);
	} else {
		rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_PG0_UARTDIV, RZN1_UART_PLL_DIV);
		rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_PG1_PR2DIV, RZN1_UART_PLL_DIV);
	}

#if defined(RZN1_DONT_TURN_OFF_CLOCKS)
	rzn1_start_usb_compat();
#endif

	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_NFLASHDIV, RZN1_NAND_PLL_DIV);

	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_QSPI0DIV, RZN1_QSPI_PLL_DIV);

#if defined (CONFIG_ARCH_RZN1S)
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_QSPI1DIV, RZN1_QSPI_PLL_DIV);
#endif

	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_PG0_I2CDIV, RZN1_I2C_PLL_DIV);

	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_SDIO0DIV, RZN1_SDHC_PLL_DIV);
	sysctrl_writel(SDHC_CLK_MHZ, RZN1_SYSCTRL_REG_CFG_SDIO0);

#if !defined (CONFIG_ARCH_RZN1L)
	rzn1_sysctrl_div(RZN1_SYSCTRL_REG_PWRCTRL_SDIO1DIV, RZN1_SDHC_PLL_DIV);
	sysctrl_writel(SDHC_CLK_MHZ, RZN1_SYSCTRL_REG_CFG_SDIO1);
#endif

	/* Enable pinmux clocks and FlexWAY connection for UART */
	rzn1_clk_set_gate(RZN1_HCLK_PINCONFIG_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_GPIO0_ID, 1);
	rzn1_clk_set_gate(RZN1_HCLK_GPIO1_ID, 1);

	/* Enable UART clock and FlexWAY connection */
	rzn1_clk_set_gate(RZN1_HCLK_UART0_ID, 1);
	rzn1_clk_set_gate(RZN1_CLK_UART0_ID, 1);

	/* ROM needed, 2nd core in BootROM and SPL calls BootROM functions */
	rzn1_clk_set_gate(RZN1_HCLK_ROM_ID, 1);

	rzn1_setup_pinmux();

	return 0;
}

static bool rzn1_usb_func_en = true;
void rzn1_uses_usb_func(bool func)
{
	rzn1_usb_func_en = func;
}

/* Configure clocks for the USB blocks */
int rzn1_usb_init(int index, enum usb_init_type init)
{
	rzn1_enable_usb_pll(!rzn1_usb_func_en);

	/* Enable USB clocks */
	if (init == USB_INIT_HOST) {
		rzn1_clk_set_gate(RZN1_HCLK_USBH_ID, 1);
		rzn1_clk_set_gate(RZN1_HCLK_USBPM_ID, 1);
		rzn1_clk_set_gate(RZN1_CLK_PCI_USB_ID, 1);
	}
	if (init == USB_INIT_DEVICE)
		rzn1_clk_set_gate(RZN1_HCLK_USBF_ID, 1);

	return 0;
}

int rzn1_usb_cleanup(int index, enum usb_init_type init)
{
	/*
	 * TODO Turn off the USB clock when we don't have to worry about old
	 * versions of Linux, see rzn1_start_usb_compat()
	 */
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

/*
 * Helper func to pulse a GPIO. The GPIO is specified as a DT node and property.
 * This only works in SPL if the dt_node is marked with "u-boot,dm-pre-reloc;"
 * and the DT node for the GPIO driver that the pin belongs to is also marked
 * with "u-boot,dm-pre-reloc;"
 */
int fdt_pulse_gpio(const char *dt_node, const char *dt_prop, int ms)
{
	struct gpio_desc reset_gpio = {};
	int node;
	int ret;

	node = fdt_node_offset_by_compatible(gd->fdt_blob, 0, dt_node);
	if (node < 0)
		return node;

	ret = gpio_request_by_name_nodev(gd->fdt_blob, node, dt_prop, 0,
				 &reset_gpio, GPIOD_IS_OUT);
	if (ret)
		return ret;

	/* reset the phy */
	ret = dm_gpio_set_value(&reset_gpio, 0);
	if (ret)
		return ret;

	mdelay(ms);

	ret = dm_gpio_set_value(&reset_gpio, 1);
	if (ret)
		return ret;

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

#if defined (CONFIG_CPU_V7M)
static void start_cm3(void)
{
	/*
	 * U-Boot is already running on the Cortex M3, start the code by setting
	 * the SP and PC as they would from a reset.
	 */
	u32 sp = readl(RZN1_SRAM_ID_BASE);
	u32 pc = readl(RZN1_SRAM_ID_BASE + 4);

	__asm__ __volatile__("mov sp, %0"
			     :
			     : "r" (sp)
			     : "memory");
	__asm__ __volatile__("mov pc, %0"
			     :
			     : "r" (pc)
			     : "memory");
}
#endif

/* Enable the Cortex M3 clock and it starts executing, IVT is at 0x04000000 */
static int do_rzn1_start_cm3(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	struct ipc_msg msg = { 0 };

	msg.msg_type = 1;
	msg.wait_rsp = 0;
#if defined(CONFIG_ENV_OFFSET)
	msg.env_ptr = RZN1_V_QSPI_BASE + CONFIG_ENV_OFFSET;
#else
	msg.env_ptr = 0;
#endif

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

#if defined (CONFIG_CPU_V7M)
	start_cm3();
#else
	rzn1_clk_set_gate(RZN1_HCLK_CM3_ID, 1);

	/* Reset then enable the Cortex M3 clock */
	sysctrl_writel(0x5, RZN1_SYSCTRL_REG_PWRCTRL_CM3);
	udelay(10);
	sysctrl_writel(0x3, RZN1_SYSCTRL_REG_PWRCTRL_CM3);

	/* Wait for the CM3 to send a msg back */
	if (msg.wait_rsp)
		ipc_recv_all(IPC_RX_MBOX, (void *)&msg);
#endif

	return 0;
}

U_BOOT_CMD(
	rzn1_start_cm3, 3, 0, do_rzn1_start_cm3,
	"Start Cortex M3 core",
	   "<wait> 0x<address>\n"
	   "    - wait = 0|1: Wait for cm3 to ack using IPCM register\n"
	   "    - Pass the address specified via the IPCM register\n"
);
