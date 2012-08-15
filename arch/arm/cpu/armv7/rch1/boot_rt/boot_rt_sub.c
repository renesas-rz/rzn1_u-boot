/*
 * boot_rt_sub.c
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

#define RESET_SH4ARESETVEC	(0xFE400040)
#define CPG_SH4ASTBCR		(0xFE400004)
#define INTC2_MSKRG			(0xFE782040)
#define SHIICR0				(0xFE700030)
#define AIICR0				(0xFE700070)

#define REG_IMGADDR			RESET_SH4ARESETVEC
#define REG_RTCPUCLOCK		CPG_SH4ASTBCR
#define REG_INT				INTC2_MSKRG
#define REG_RTIIC			SHIICR0
#define REG_ARMIIC			AIICR0

#ifdef CONFIG_AUTO_BOOT_RT
int read_rt_image(unsigned int *addr)
#else
int read_rt_image(unsigned int *addr, int argc, char* const argv[])
#endif
{
	struct rt_boot_info info;
	struct rt_boot_info *bootaddr_info;

	unsigned char *src;
	unsigned char *dst;

	if (addr == NULL) {
		DBG_PRINT("addr is NULL\n");
		return 1;
	}

	/* copy RTImage from NOR */
	DBG_PRINT("copy RTImage from NOR\n");
#ifndef CONFIG_AUTO_BOOT_RT
	if (argc < 2) {
#endif
		src = (unsigned char *)CONFIG_RT_IMAGE_SRC;
#ifndef CONFIG_AUTO_BOOT_RT
	} else {
		src = (unsigned char *)simple_strtoul(argv[1], NULL, 16);
	}
#endif

	/* Get RT boot info */
	memcpy(&info, src + RT_BOOT_SIZE, sizeof(struct rt_boot_info));
	DBG_PRINT("Read RT Section header from RT Image in NOR\n");
	DBG_PRINT("version                       = %c%c%c%c%c%c%c%c%c%c%c%c\n",
															info.version[0],
															info.version[1],
															info.version[2],
															info.version[3],
															info.version[4],
															info.version[5],
															info.version[6],
															info.version[7],
															info.version[8],
															info.version[9],
															info.version[10],
															info.version[11]);
	DBG_PRINT("boot_addr                     = 0x%08x\n",	info.boot_addr);
	DBG_PRINT("image_size                    = %08d\n",		info.image_size);
	DBG_PRINT("load_flg                      = %08d\n",		info.load_flg);
	DBG_PRINT("img[RT_LEVEL_1].section_start = 0x%08x\n",	info.img[RT_LEVEL_1].section_start);
	DBG_PRINT("img[RT_LEVEL_1].section_size  = %08d\n",		info.img[RT_LEVEL_1].section_size);
	DBG_PRINT("img[RT_LEVEL_2].section_start = 0x%08x\n",	info.img[RT_LEVEL_2].section_start);
	DBG_PRINT("img[RT_LEVEL_2].section_size  = %08d\n",		info.img[RT_LEVEL_2].section_size);

	/* Copy RT image */
	dst = (unsigned char *)info.boot_addr + info.img[RT_LEVEL_1].section_start;
	memcpy(dst, src, info.img[RT_LEVEL_1].section_size);
	*addr = info.boot_addr;

	/* Init load_flg */
	bootaddr_info = (struct rt_boot_info *)(info.boot_addr + RT_BOOT_SIZE);
	bootaddr_info->load_flg = 0;

	DBG_PRINT("RTimage addr=0x%x size=%d\n", info.boot_addr, info.img[RT_LEVEL_1].section_size);
	return 0;
}


void write_rt_imageaddr(unsigned int addr)
{
	/* Write RT image addr to register */
	outl(addr, REG_IMGADDR);
}


void stop_rt_interrupt(void)
{
	outl(0xFFFFFFFF, REG_INT);
}

void init_rt_register(void)
{
	outl(0x00000000, REG_RTIIC);
}

void write_os_kind(unsigned int kind)
{
	outl(kind, REG_ARMIIC);
}

void start_rt_cpu(void)
{
	/* Enable RT clock */
	outl(inl(REG_RTCPUCLOCK) & ~0x00000001, REG_RTCPUCLOCK);
}


int wait_rt_cpu(unsigned int check_num)
{
	int ret = 1;
	unsigned int loop = check_num;

	while (loop != 0) {
		if (inl(REG_ARMIIC) == 0) {
			ret = 0;
			break;
		}
		DBG_PRINT("RT boot wating...\n");
#if !(DEBUG)
		--loop;
#endif
	}
	return ret;
}

#endif	/* CONFIG_BOOT_RT */
