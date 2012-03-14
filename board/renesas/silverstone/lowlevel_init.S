/*
 * board/renesas/silverstone/lowlevel_init.S
 *
 * Copyright (C) 2011 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <config.h>
#include <version.h>

	.globl lowlevel_init
lowlevel_init:
	ldr	r3, =0xfe798000
	sub	sp, r3, #4
	str	ip, [sp]
	mov	ip, lr

	/* select pin */
	bl	pin_init

	/* initialize exbus */
	bl	exbus_init

	/* initialize ddr */
	bl	memc_init

	ldr	ip, [sp]
	mov	lr, ip
	mov	pc, lr
	nop
