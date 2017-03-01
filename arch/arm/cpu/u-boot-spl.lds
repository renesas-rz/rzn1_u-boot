/*
 * Copyright (c) 2004-2008 Texas Instruments
 *
 * (C) Copyright 2002
 * Gary Jennejohn, DENX Software Engineering, <garyj@denx.de>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
SECTIONS
{
	. = 0x00000000;

	. = ALIGN(4);
	.text :
	{
		__image_copy_start = .;
		*(.vectors)
		CPUDIR/start.o (.text*)
		*(.text*)
	}

#ifdef CONFIG_ARMV7_NONSEC

	/* Align the secure section only if we're going to use it in situ */
	.__secure_start :
#ifndef CONFIG_ARMV7_SECURE_BASE
	/*	ALIGN(CONSTANT(COMMONPAGESIZE)) */
		ALIGN(32)
#endif
	{
		KEEP(*(.__secure_start))
	}

#ifndef CONFIG_ARMV7_SECURE_BASE
#define CONFIG_ARMV7_SECURE_BASE
#define __ARMV7_PSCI_STACK_IN_RAM
#endif

	.secure_text CONFIG_ARMV7_SECURE_BASE :
		AT(ADDR(.__secure_start) + SIZEOF(.__secure_start))
	{
		*(._secure.text)
	}

	.secure_data : AT(LOADADDR(.secure_text) + SIZEOF(.secure_text))
	{
		*(._secure.data)
	}

#ifdef CONFIG_ARMV7_PSCI
	.secure_stack ALIGN(ADDR(.secure_data) + SIZEOF(.secure_data),
			    CONSTANT(COMMONPAGESIZE)) (NOLOAD) :
#ifdef __ARMV7_PSCI_STACK_IN_RAM
		AT(ADDR(.secure_stack))
#else
		AT(LOADADDR(.secure_data) + SIZEOF(.secure_data))
#endif
	{
		KEEP(*(.__secure_stack_start))

		/* Skip addreses for stack */
		. = . + CONFIG_ARMV7_PSCI_NR_CPUS * ARM_PSCI_STACK_SIZE;

		/* Align end of stack section to page boundary */
		. = ALIGN(CONSTANT(COMMONPAGESIZE));

		KEEP(*(.__secure_stack_end))

#ifdef CONFIG_ARMV7_SECURE_MAX_SIZE
		/*
		 * We are not checking (__secure_end - __secure_start) here,
		 * as these are the load addresses, and do not include the
		 * stack section. Instead, use the end of the stack section
		 * and the start of the text section.
		 */
		ASSERT((. - ADDR(.secure_text)) <= CONFIG_ARMV7_SECURE_MAX_SIZE,
		       "Error: secure section exceeds secure memory size");
#endif
	}

#ifndef __ARMV7_PSCI_STACK_IN_RAM
	/* Reset VMA but don't allocate space if we have secure SRAM */
	. = LOADADDR(.secure_stack);
#endif

#endif

	.__secure_end : AT(ADDR(.__secure_end)) {
		*(.__secure_end)
		LONG(0x1d1071c);	/* Must output something to reset LMA */
	}
#endif

	. = ALIGN(4);
	.rodata : { *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*))) }

	. = ALIGN(4);
	.data : {
		*(.data*)
	}

	. = ALIGN(4);
	.u_boot_list : {
		KEEP(*(SORT(.u_boot_list*)));
	}

	. = ALIGN(4);

	__image_copy_end = .;

	.rel.dyn : {
		__rel_dyn_start = .;
		*(.rel*)
		__rel_dyn_end = .;
	}

	.end :
	{
		*(.__end)
	}

	_image_binary_end = .;

	.bss __rel_dyn_start (OVERLAY) : {
		__bss_start = .;
		*(.bss*)
		 . = ALIGN(4);
		__bss_end = .;
	}
	__bss_size = __bss_end - __bss_start;
	.dynsym _image_binary_end : { *(.dynsym) }
	.dynbss : { *(.dynbss) }
	.dynstr : { *(.dynstr*) }
	.dynamic : { *(.dynamic*) }
	.hash : { *(.hash*) }
	.plt : { *(.plt*) }
	.interp : { *(.interp*) }
	.gnu : { *(.gnu*) }
	.ARM.exidx : { *(.ARM.exidx*) }
}

#if defined(CONFIG_SPL_MAX_SIZE)
ASSERT(__image_copy_end - __image_copy_start < (CONFIG_SPL_MAX_SIZE), \
	"SPL image too big");
#endif

#if defined(CONFIG_SPL_BSS_MAX_SIZE)
ASSERT(__bss_end - __bss_start < (CONFIG_SPL_BSS_MAX_SIZE), \
	"SPL image BSS too big");
#endif

#if defined(CONFIG_SPL_MAX_FOOTPRINT)
ASSERT(__bss_end - _start < (CONFIG_SPL_MAX_FOOTPRINT), \
	"SPL image plus BSS too big");
#endif
