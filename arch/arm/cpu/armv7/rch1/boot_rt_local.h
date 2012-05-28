/*
 * boot_rt_local.h
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

#ifndef __BOOT_RT_LOCAL_H__
#define __BOOT_RT_LOCAL_H__

#define DEBUG 1
#if DEBUG
#define DBG_PRINT(FMT, ARGS...)	printf(FMT, ##ARGS)
#else
#define DBG_PRINT(FMT, ARGS...)
#endif

#define __io

/* boot kind */
enum {
	EMMCBOOT = 0,
	SDBOOT,
	NORBOOT
};

/* RT boot_program size */
#define RT_BOOT_SIZE		0xb00

enum {
	RT_LEVEL_1 = 0,
	RT_LEVEL_2,
	RT_LEVEL_MAX
};

struct rt_section_img_info {
	unsigned int	section_start;		/* start address of section */
	unsigned int	section_size;		/* size of section	*/
};

struct rt_boot_info {
	unsigned char	version[12];
	unsigned int	table_size;						/* unused */
	unsigned int	boot_addr;
	unsigned int	image_size;
	unsigned int	memmpl_address;					/* unused */
	unsigned int	memmpl_size;					/* unused */
	unsigned int	command_area_address;			/* unused */
	unsigned int	command_area_size;				/* unused */
	unsigned int	load_flg;
	struct rt_section_img_info img[RT_LEVEL_MAX];	/* section image information */
};

#ifdef CONFIG_AUTO_BOOT_RT
int		boot_rt(void);
int		read_rt_image(unsigned int *addr);
#else
int		do_boot_rt(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[]);
int		read_rt_image(unsigned int *addr, int argc, char* const argv[]);
#endif
void	write_rt_imageaddr(unsigned int addr);
void	stop_rt_interrupt(void);
void	init_rt_register(void);
void	write_os_kind(unsigned int kind);
void	start_rt_cpu(void);
int		wait_rt_cpu(unsigned int check_num);
#endif

