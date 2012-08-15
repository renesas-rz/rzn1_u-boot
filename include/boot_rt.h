/*
 * boot_rt.h
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

#ifndef	__BOOT_RT_H__
#define	__BOOT_RT_H__

#define CONFIG_BOOT_RT
#ifdef CONFIG_BOOT_RT
#define CONFIG_RT_IMAGE_SRC	0x02000000
/* #define CONFIG_AUTO_BOOT_RT */
#ifdef CONFIG_AUTO_BOOT_RT
extern int boot_rt(void);
#endif	/* CONFIG_AUTO_BOOT_RT */
#endif	/* CONFIG_BOOT_RT */
#endif	/* __BOOT_RT_H__ */
