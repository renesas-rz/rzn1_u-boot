/*
 * cmd_dfu_ext.c -- extended dfu command
 *
 * Copyright (C) 2012 Samsung Electronics
 * authors: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *	    Lukasz Majewski <l.majewski@samsung.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dfu.h>
#include <g_dnl.h>
#include <usb.h>

static int do_dfu(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	int ret, i = 0;

	ret = dfu_init_env_entities(NULL, 0);
	if (ret)
		goto done;

	ret = CMD_RET_SUCCESS;
	if (argc > 1 && !strcmp(argv[1], "list")) {
		dfu_show_entities();
		goto done;
	}

	int controller_index = simple_strtoul(getenv("dfu_ext_intf"), NULL, 10);

	board_usb_init(controller_index, USB_INIT_DEVICE);

	g_dnl_register("usb_dnl_dfu");
	while (1) {
		if (dfu_reset())
			/*
			 * This extra number of usb_gadget_handle_interrupts()
			 * calls is necessary to assure correct transmission
			 * completion with dfu-util
			 */
			if (++i == 10)
				goto exit;

		if (ctrlc())
			goto exit;

		usb_gadget_handle_interrupts();
	}
exit:
	g_dnl_unregister();
done:
	dfu_free_entities();

	if (dfu_reset())
		run_command("reset", 0);

	return ret;
}

U_BOOT_CMD(dfu, CONFIG_SYS_MAXARGS, 1, do_dfu,
	   "Device Firmware Upgrade",
	" [list]\n"
	"    [list] - list available DFU settings\n"
);
