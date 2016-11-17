/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	BSD-2-Clause
 */

#ifndef __RZN1_UTILS_H__
#define __RZN1_UTILS_H__

#include <usb.h>
#include "renesas/rzn1-memory-map.h"

/* System Controller */
#define sysctrl_readl(addr) \
	readl(RZN1_SYSTEM_CTRL_BASE + addr)
#define sysctrl_writel(val, addr) \
	writel(val, RZN1_SYSTEM_CTRL_BASE + addr)

int rzn1_clk_set_gate(int clkdesc_id, int on);
void rzn1_clk_reset(int clkdesc_id);
void rzn1_clk_reset_state(int clkdesc_id, int level);

void rzn1_sysctrl_div(u32 reg, u32 div);

static inline int is_rzn1d(void)
{
	u32 ver = sysctrl_readl(RZN1_SYSCTRL_REG_VERSION);
	if ((ver >> RZN1_SYSCTRL_REG_VERSION_PROD) == 0)
		return 1;
	return 0;
}

/* USB */
int rzn1_usb_init(int index, enum usb_init_type init);

/* PinMux */
#define RZN1_DRIVE_SET  	(1 << 31)
#define RZN1_PULL_SET  		(1 << 30)
#define RZN1_DRIVE_4MA		(RZN1_DRIVE_SET | (0 << 10))
#define RZN1_DRIVE_6MA		(RZN1_DRIVE_SET | (1 << 10))
#define RZN1_DRIVE_8MA		(RZN1_DRIVE_SET | (2 << 10))
#define RZN1_DRIVE_12MA		(RZN1_DRIVE_SET | (3 << 10))
#define RZN1_DRIVE_MASK		(3 << 10)
#define RZN1_PULL_NONE		(RZN1_PULL_SET | (0 << 8))
#define RZN1_PULL_UP		(RZN1_PULL_SET | (1 << 8))
#define RZN1_PULL_DOWN		(RZN1_PULL_SET | (3 << 8))
#define RZN1_PULL_MASK		(3 << 8)

void rzn1_pinmux_set(u32 setting);
void rzn1_pinmux_select(u8 pin, u32 func, u32 attrib);

/* Ethernet */
void rzn1_mt5pt_switch_enable_port(int port);
void rzn1_mt5pt_switch_setup_port_speed(int port, int speed);
void rzn1_mt5pt_switch_init(void);

void rzn1_rin_prot_writel(u32 val, u32 reg);
void rzn1_rin_switchcore_setup(int port, int duplex, int speed);
void rzn1_switch_setup_port_speed(int port, int speed, int enable);
void rzn1_rgmii_rmii_conv_setup(int phy, u32 if_type, int rmii_ref_clk_out);
void rzn1_rgmii_rmii_conv_speed(int phy, int duplex, int speed);
void rzn1_rin_init(void);
void rzn1_rin_reset_clks(void);

/* DDR */
#define RZN1_DDR2 2
#define RZN1_DDR3_SINGLE_BANK 3
#define RZN1_DDR3_DUAL_BANK 32
void ddr_phy_init(int ddr_type);
void ddr_phy_enable_wl(void);
void rzn1_ddr_ctrl_init(const u32 *reg0, const u32 *reg350, u32 ddr_size);

#endif /* __RZN1_UTILS_H__ */
