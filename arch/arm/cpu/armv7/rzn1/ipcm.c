/*
 * Simple driver for the ARM PL320 IPCM
 *
 * Based on Linux drivers/mailbox/pl320-ipc.c
 * Copyright 2012 Calxeda, Inc.
 *
 * SPDX-License-Identifier:	GPL-2.0
 */
#include <common.h>
#include <asm/io.h>
#include "ipcm.h"

#define IPCMxSOURCE(m)		((m) * 0x40)
#define IPCMxDSET(m)		(((m) * 0x40) + 0x004)
#define IPCMxDSTATUS(m)		(((m) * 0x40) + 0x00c)
#define IPCMxMSET(m)		(((m) * 0x40) + 0x014)
#define IPCMxSEND(m)		(((m) * 0x40) + 0x020)
 #define IPCMxSEND_SEND		BIT(0)
 #define IPCMxSEND_ACK		BIT(1)
#define IPCMxDR(m, dr)		(((m) * 0x40) + ((dr) * 4) + 0x024)

#define IPCMRIS(irq)		(((irq) * 8) + 0x804)
#define IPCMCFGSTAT		0x900
#define  NR_MBOXES(x)		(((x) >> 16) & 63)
#define  NR_DATAWORDS(x)	((x) & 7)

#define ALL_CHANS(x)		((1 << (x)) - 1)

static uintptr_t ipc_base;
static uint32_t nr_mboxes;
static uint32_t nr_datawords;
static uint32_t all_chans;

void ipc_init(uintptr_t _ipc_base)
{
	ipc_base = _ipc_base;
	nr_mboxes = NR_MBOXES(readl(ipc_base + IPCMCFGSTAT));
	nr_datawords = NR_DATAWORDS(readl(ipc_base + IPCMCFGSTAT));
	all_chans = ALL_CHANS(nr_mboxes);
}

void ipc_setup_1to1(uint32_t tx_mbox, uint32_t rx_mbox)
{
	uint32_t rx_mboxes = IPC_CHAN_MASK(rx_mbox);

	ipc_setup_custom(tx_mbox, rx_mboxes);
}

void ipc_setup_broadcast(uint32_t tx_mbox)
{
	uint32_t rx_mboxes = all_chans - IPC_CHAN_MASK(tx_mbox);

	ipc_setup_custom(tx_mbox, rx_mboxes);
}

void ipc_setup_custom(uint32_t tx_mbox, uint32_t rx_mboxes)
{
	writel(0, ipc_base + IPCMxSEND(tx_mbox));
	writel(IPC_CHAN_MASK(tx_mbox), ipc_base + IPCMxSOURCE(tx_mbox));
	writel(rx_mboxes, ipc_base + IPCMxDSET(tx_mbox));
	writel(rx_mboxes, ipc_base + IPCMxMSET(tx_mbox));
}

void ipc_send(uint32_t tx_mbox, uint32_t *data)
{
	int i;

	for (i = 0; i < nr_datawords; i++)
		writel(data[i], ipc_base + IPCMxDR(tx_mbox, i));
	writel(IPCMxSEND_SEND, ipc_base + IPCMxSEND(tx_mbox));
}

void ipc_wait_for_ack(uint32_t tx_mbox)
{
	while (!(readl(ipc_base + IPCMxSEND(tx_mbox)) & IPCMxSEND_ACK))
		;
}

/* Note that this blocks */
void ipc_recv(uint32_t rx_mbox, uint32_t from_mboxes, uint32_t *data)
{
	int i;

	while (!(readl(ipc_base + IPCMxDSTATUS(rx_mbox)) & from_mboxes))
		;

	for (i = 0; i < nr_datawords; i++)
		data[i] = readl(ipc_base + IPCMxDR(rx_mbox, i));

	writel(IPCMxSEND_ACK, ipc_base + IPCMxSEND(rx_mbox));
}

void ipc_recv_all(uint32_t rx_mbox, uint32_t *data)
{
	ipc_recv(rx_mbox, all_chans, data);
}
