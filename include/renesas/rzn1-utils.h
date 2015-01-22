/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __RZN1_UTILS_H__
#define __RZN1_UTILS_H__

#include "renesas/rzn1-memory-map.h"

/* System Controller */
#define sysctrl_readl(addr) \
	readl(RZN1_SYSTEM_CTRL_BASE + addr)
#define sysctrl_writel(val, addr) \
	writel(val, RZN1_SYSTEM_CTRL_BASE + addr)

void rzn1_clk_set_gate(int clkdesc_id, int on);
void rzn1_clk_reset(int clkdesc_id);

void rzn1_sysctrl_div(u32 reg, u32 div);

/* PinMux */
#define RZN1_RGMII_MODE_HSTL	(1 << 12)
#define RZN1_RGMII_MODE_LVTTL	(0 << 12)
#define RZN1_DRIVE_7MA		(0 << 10)
#define RZN1_DRIVE_8MA		(1 << 10)
#define RZN1_DRIVE_9MA		(2 << 10)
#define RZN1_DRIVE_10MA		(3 << 10)
#define RZN1_PULL_NONE		(0 << 8)
#define RZN1_PULL_UP		(1 << 8)
#define RZN1_PULL_NONE2		(2 << 8)
#define RZN1_PULL_DOWN		(3 << 8)

void rzn1_pinmux_select(u8 pin, u32 func, u32 attrib);

/* Ethernet */
void rzn1_rin_switchcore_setup(int port, int speed);
void rzn1_mt5pt_switch_enable_port(int port);
void rzn1_mt5pt_switch_setup_port_speed(int port, int speed);
void rzn1_switch_setup_port_speed(int port, int speed, int enable);
void rzn1_mt5pt_switch_init(void);
void rzn1_rin_rgmii_rmii_conv_setup_phy(int phy, u32 if_type, int speed);
void rzn1_rin_init(void);
void rzn1_rin_reset_clks(void);

#endif /* __RZN1_UTILS_H__ */
