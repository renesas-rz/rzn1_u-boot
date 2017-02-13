/*
 * (C) Copyright 2015
 * Kamil Lulko, <kamil.lulko@gmail.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

.globl	reset
.type reset, %function
reset:
	ldr	r0, =_main
	mov	pc, r0

.globl	c_runtime_cpu_setup
c_runtime_cpu_setup:
	mov	pc, lr
