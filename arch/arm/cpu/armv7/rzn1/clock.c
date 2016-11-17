/*
 * (C) Copyright 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	BSD-2-Clause
 */
#include <asm/io.h>
#include <common.h>
#include "renesas/rzn1-memory-map.h"
#include "renesas/rzn1-utils.h"
#include "renesas/rzn1-clocks.h"

/* Generic register/bit group descriptor */
struct rzn1_onereg {
	uint16_t reg : 7,	/* Register number (word) */
		pos : 5,	/* Bit number */
		size : 4;	/* Optional: size in bits */
} __attribute__((packed));

struct rzn1_clkdesc {
	struct rzn1_onereg clock, reset, ready, masteridle, scon, mirack, mistat;
} __attribute__((packed));

#define _BIT(_r, _p) { .reg = _r, .pos = _p }

#define _CLK(_n, _clk, _rst, _rdy, _midle, _scon, _mirack, _mistat) \
	{ .clock = _clk, .reset = _rst, .ready = _rdy, .masteridle = _midle, \
	  .scon = _scon, .mirack = _mirack, .mistat = _mistat }

#include "renesas/rzn1-clkctrl-tables.h"

#define clk_readl readl
#define clk_writel writel

static void *clk_mgr_base_addr = (void *)RZN1_SYSTEM_CTRL_BASE;

static void clk_mgr_desc_set(const struct rzn1_onereg *one, int on)
{
	u32 *reg = ((u32 *)clk_mgr_base_addr) + one->reg;
	u32 val = clk_readl(reg);
	val = (val & ~(1 << one->pos)) | ((!!on) << one->pos);
	clk_writel(val, reg);
}

static u32 clk_mgr_desc_get(const struct rzn1_onereg *one)
{
	u32 *reg = ((u32 *)clk_mgr_base_addr) + one->reg;
	u32 val = clk_readl(reg);
	val = (val >> one->pos) & 1;
	return val;
}

int rzn1_clk_set_gate(int clkdesc_id, int on)
{
	const struct rzn1_clkdesc *g = &rzn1_clock_list[clkdesc_id];
	u32 timeout;

	BUG_ON(!clk_mgr_base_addr);
	BUG_ON(!g->clock.reg);

	if (!on && g->masteridle.reg) {
		/* Set 'Master Idle Request' bit */
		clk_mgr_desc_set(&g->masteridle, 1);

		/* Wait for Master Idle Request acknowledge */
		if (g->mirack.reg) {
			for (timeout = 100000; timeout &&
				!clk_mgr_desc_get(&g->mirack); timeout--)
					;
			if (!timeout)
				return -1;
		}

		/* Wait for Master Idle Status signal */
		if (g->mistat.reg) {
			for (timeout = 100000; timeout &&
				!clk_mgr_desc_get(&g->mistat); timeout--)
					;
			if (!timeout)
				return -1;
		}
	}

	/* Enable/disable the clock */
	clk_mgr_desc_set(&g->clock, on);

	/* If the peripheral is memory mapped (i.e. an AXI slave), there is an
	 * associated SLVRDY bit in the System Controller that needs to be set
	 * so that the FlexWAY bus fabric passes on the read/write requests.
	 */
	if (g->ready.reg)
		clk_mgr_desc_set(&g->ready, on);

	/* Clear 'Master Idle Request' bit */
	if (g->masteridle.reg)
		clk_mgr_desc_set(&g->masteridle, !on);

	/* Wait for FlexWAY Socket Connection signal */
	if (on && g->scon.reg) {
		for (timeout = 100000; timeout &&
			!clk_mgr_desc_get(&g->scon); timeout--)
				;
		if (!timeout)
			return -1;
	}

	return 0;
}

void rzn1_clk_reset(int clkdesc_id)
{
	const struct rzn1_clkdesc *g = &rzn1_clock_list[clkdesc_id];

	BUG_ON(!clk_mgr_base_addr);
	BUG_ON(!g->clock.reg);

	if (g->reset.reg) {
		clk_mgr_desc_set(&g->reset, 0);
		udelay(1);
		clk_mgr_desc_set(&g->reset, 1);
	}
}

void rzn1_clk_reset_state(int clkdesc_id, int level)
{
	const struct rzn1_clkdesc *g = &rzn1_clock_list[clkdesc_id];

	BUG_ON(!clk_mgr_base_addr);
	BUG_ON(!g->clock.reg);

	if (g->reset.reg)
		clk_mgr_desc_set(&g->reset, level);
}
