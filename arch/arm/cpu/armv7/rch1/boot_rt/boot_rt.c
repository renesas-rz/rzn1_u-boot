/*
 * boot_rt.c
 *		booting rt_cpu.
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
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

#include <common.h>
#include <command.h>
#include "boot_rt.h"
#include "boot_rt_local.h"
#include <asm/io.h>

#ifdef CONFIG_BOOT_RT
/* OS kind */
#define OS_KIND_LINUX 0x50

#ifdef CONFIG_AUTO_BOOT_RT
int boot_rt(void)
#else
int do_boot_rt(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
#endif
{
	int ret = 1;
	unsigned int bootaddr = 0;

	DBG_PRINT("RT boot start\n");

#ifdef CONFIG_AUTO_BOOT_RT
	if (0 != read_rt_image(&bootaddr)) {
#else
	if (0 != read_rt_image(&bootaddr, argc, argv)) {
#endif
		DBG_PRINT("read_RT_image() Error\n");
		return 1;
	}

	write_rt_imageaddr(bootaddr);

	stop_rt_interrupt();

	init_rt_register();

	write_os_kind(OS_KIND_LINUX);

	start_rt_cpu();

	ret = wait_rt_cpu(300);

	if (ret == 1) {
		DBG_PRINT("RT boot error\n");
		return 1;
	}

	DBG_PRINT("RT boot end\n");

	return 0;
}

#ifndef CONFIG_AUTO_BOOT_RT
U_BOOT_CMD(
	boot_rt, 2, 0, do_boot_rt,
	"Booting RT",
	" addr"
);
#endif
#endif	/* CONFIG_BOOT_RT */
