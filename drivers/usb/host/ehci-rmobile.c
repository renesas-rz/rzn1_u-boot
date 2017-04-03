/*
 *  EHCI HCD (Host Controller Driver) for USB.
 *
 *  Copyright (C) 2013,2014 Renesas Electronics Corporation
 *  Copyright (C) 2014 Nobuhiro Iwamatsu <nobuhiro.iwamatsu.yj@renesas.com>
 *
 *  SPDX-License-Identifier:     GPL-2.0
 */

#include <common.h>
#include <asm/io.h>
#include "ohci.h"
#include "ehci.h"
#include "ehci-rmobile.h"
#include <pci.h>

#if defined(CONFIG_R8A7740)
static u32 usb_base_address[] = {
	0xC6700000
};
#elif defined(CONFIG_R8A7790)
static u32 usb_base_address[] = {
	0xEE080000,	/* USB0 (EHCI) */
	0xEE0A0000,	/* USB1 */
	0xEE0C0000,	/* USB2 */
};
#elif defined(CONFIG_R8A7791) || defined(CONFIG_R8A7793) || \
	defined(CONFIG_R8A7794)
static u32 usb_base_address[] = {
	0xEE080000,	/* USB0 (EHCI) */
	0xEE0C0000,	/* USB1 */
};
#elif defined(CONFIG_ARCH_RZN1)
static u32 usb_base_address[] = {
	0x40020000,	/* USB0 (EHCI) */
};
#else
#error rmobile EHCI USB driver not supported on this platform
#endif

#if !defined(CONFIG_ARCH_RZN1)
static u32 win_address[] = {
	CONFIG_SYS_SDRAM_BASE,	/* DDR */
};
#else
/*
 * RZ/N1 is typically configured to stay in SRAM as RZ/N1S doesn't support DDR.
 * However, the default configuration means that the USB Host controller needs
 * to access both SRAMs (at 0x0400000 and 0x20000000). In order to cover both
 * of these and provide access to DDR, the large configurable PCI-AHB window is
 * set to address 0, 1GB, and the second PCI-AHB window (fixed 256MB) is set to
 * cover DDR. However, this means U-Boot cannot access more than 256MB DDR.
 */
static u32 win_address[] = {
	0x04000000,	/* SRAM, the win will also cover SRAM at 0x20000000 */
	0x80000000,	/* DDR, or another SRAM on RZ/N1S, win is only 256MB */
};
#endif

#define EHCI_USBCMD_OFF		0x20
#define EHCI_USBCMD_HCRESET	(1 << 1)

int ehci_hcd_stop(int index)
{
	int i;
	u32 base, reg, ehci;

	base = usb_base_address[index];
	reg = base + PCI_CONF_AHBPCI_OFFSET;
	ehci = base + EHCI_OFFSET;

	writel(0, reg + RCAR_AHB_BUS_CTR_REG);

	/* reset ehci */
	setbits_le32(ehci + EHCI_USBCMD_OFF, EHCI_USBCMD_HCRESET);
	for (i = 100; i > 0; i--) {
		if (!(readl(ehci + EHCI_USBCMD_OFF) & EHCI_USBCMD_HCRESET))
			break;
		udelay(100);
	}

	if (!i)
		printf("error : ehci(%d) reset failed.\n", index);

#if !defined(CONFIG_ARCH_RZN1)
	/* Turn off IP clock */
	if (index == (ARRAY_SIZE(usb_base_address) - 1))
		setbits_le32(SMSTPCR7, SMSTPCR703);
#endif

	return 0;
}

static u32 pci_config_fn(u32 reg, int devfn, u32 addr)
{
	/* devfn 0 is the root */
	/* devfn 1 is OHCI */
	/* devfn 2 is ECHI */
	u32 ctrl = devfn ? RCAR_AHBPCI_WIN1_DEVICE | RCAR_AHBPCI_WIN_CTR_CFG :
		     RCAR_AHBPCI_WIN1_HOST | RCAR_AHBPCI_WIN_CTR_CFG;
	u32 tmp = readl(reg + RCAR_AHBPCI_WIN1_CTR_REG);

	if (tmp != ctrl)
		writel(ctrl, reg + RCAR_AHBPCI_WIN1_CTR_REG);

	/* this simple redirects to the ECHI or OCHI config space */
	if (devfn)
		addr += (devfn - 1) * 0x100;

	return addr;
}

static void pci_writel_fn(u32 reg, int devfn, u32 val, u32 addr)
{
	addr = pci_config_fn(reg, devfn, addr);
	writel(val, reg + addr);
}

static void pci_writew_fn(u32 reg, int devfn, u16 val, u32 addr)
{
	addr = pci_config_fn(reg, devfn, addr);
	writew(val, reg + addr);
}

/*
 * This code sets up the PCI Bridge so that there is:
 *  * Two outbound (AHB to PCI) address windows:
 *     a) the PCI Bridge Configuration registers
 *     b) the OHCI/EHCI Configuration registers.
 *  * One inbound (PCI to AHB) address window, so the OHCI/EHCI controller has
 *    read/write access to the data in DDR.
 */
int ehci_hcd_init(int index, enum usb_init_type init,
	struct ehci_hccr **hccr, struct ehci_hcor **hcor)
{
	u32 reg;
	struct ohci_regs *rohci;
	struct rmobile_ehci_reg *rehci;
	uint32_t cap_base;
	u32 val;
	int use_win2_addr = 0;
	u32 base = usb_base_address[index];

	win_address[0] &= 0xc0000000;	/* mask for 1GB window */
	if (sizeof(win_address) > sizeof(win_address[0])) {
		win_address[1] &= 0xf0000000;	/* mask for 256MB window */
		use_win2_addr = 1;
	}

#if !defined(CONFIG_ARCH_RZN1)
	/* Turn on IP clock */
	if (index == 0)
		clrbits_le32(SMSTPCR7, SMSTPCR703);
#endif

	reg = base + PCI_CONF_AHBPCI_OFFSET;
	rohci = (struct ohci_regs *)(base + OHCI_OFFSET);
	rehci = (struct rmobile_ehci_reg *)(base + EHCI_OFFSET);

	/* Disable Direct Power Down State and assert reset */
	val = readl(reg + RCAR_USBCTR_REG);
	val &= ~RCAR_USBCTR_DIRPD;
	val |= RCAR_USBCTR_USBH_RST;
	writel(val, reg + RCAR_USBCTR_REG);
	udelay(4);

	/* De-assert reset and set PCIAHB window1 size */
	val &= ~(RCAR_USBCTR_PCIAHB_WIN1_MASK | RCAR_USBCTR_PCICLK_MASK |
		 RCAR_USBCTR_USBH_RST | RCAR_USBCTR_PLL_RST);
	val |= RCAR_USBCTR_PCIAHB_WIN1_1G;
	if (use_win2_addr)
		val |= RCAR_USBCTR_PCIAHB_WIN2_EN;
	writel(val, reg + RCAR_USBCTR_REG);
	udelay(100);

	/* Configure AHB master and slave modes */
	writel(RCAR_AHB_BUS_MODE, reg + RCAR_AHB_BUS_CTR_REG);

	/* Configure PCI arbiter */
	val = readl(reg + RCAR_PCI_ARBITER_CTR_REG);
	val |= RCAR_PCI_ARBITER_PCIREQ0 | RCAR_PCI_ARBITER_PCIREQ1 |
	       RCAR_PCI_ARBITER_PCIBP_MODE;
	writel(val, reg + RCAR_PCI_ARBITER_CTR_REG);

	/* PCI-AHB mapping */
	writel(win_address[0] | RCAR_PCIAHB_PREFETCH16,
		  reg + RCAR_PCIAHB_WIN1_CTR_REG);

	if (use_win2_addr)
		writel(win_address[1] | RCAR_PCIAHB_PREFETCH16,
			reg + RCAR_PCIAHB_WIN2_CTR_REG);

	/* AHB-PCI mapping: OHCI/EHCI registers */
	val = base | RCAR_AHBPCI_WIN_CTR_MEM;
	writel(val, reg + RCAR_AHBPCI_WIN2_CTR_REG);

	/* Configure interrupts */
	writel(RCAR_PCI_INT_ALLERRORS, reg + RCAR_PCI_INT_ENABLE_REG);

	/* Set AHB-PCI bridge PCI communication area address */
	val = reg;
	pci_writel_fn(reg, 0, val, PCI_BASE_ADDRESS_0);

	/* Set PCI-AHB Window1 address */
	val = win_address[0] | PCI_BASE_ADDRESS_MEM_PREFETCH;
	pci_writel_fn(reg, 0, val, PCI_BASE_ADDRESS_1);

	if (use_win2_addr) {
		val = win_address[1] | PCI_BASE_ADDRESS_MEM_PREFETCH;
		pci_writel_fn(reg, 0, val, PCI_BASE_ADDRESS_2);
	}

	val = PCI_COMMAND_SERR | PCI_COMMAND_PARITY | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	pci_writew_fn(reg, 0, val, PCI_COMMAND);

	/* PCI Configuration Registers for EHCI/OHCI */
	pci_writel_fn(reg, 1, base + OHCI_OFFSET, PCI_BASE_ADDRESS_0);
	pci_writew_fn(reg, 1, val, PCI_COMMAND);
	pci_writel_fn(reg, 2, base + EHCI_OFFSET, PCI_BASE_ADDRESS_0);
	pci_writew_fn(reg, 2, val, PCI_COMMAND);

	*hccr = (struct ehci_hccr *)((uint32_t)&rehci->hciversion);
	cap_base = ehci_readl(&(*hccr)->cr_capbase);
	*hcor = (struct ehci_hcor *)((uint32_t)*hccr + HC_LENGTH(cap_base));


	/* OHCI init */
#define	USBH_POTPGT_WAIT_TIME			(0xFF << 24)
#define	USBH_NOCP_ENABLE			0
#define	USBH_OCPM_PORT_UNIT			BIT(11)
#define	USBH_NPS_ALWAYS_POWERON			BIT(9)
#define	USBH_PSM_PORT_UNIT			BIT(8)
	writel(USBH_POTPGT_WAIT_TIME | USBH_NOCP_ENABLE | USBH_OCPM_PORT_UNIT | USBH_NPS_ALWAYS_POWERON | USBH_PSM_PORT_UNIT, &rohci->roothub.a);

	return 0;
}
