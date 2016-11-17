/*
 * Simple driver for the ARM PL320 IPCM
 *
 * SPDX-License-Identifier:	GPL-2.0
 */
#ifndef __IPCM_H__
#define __IPCM_H__

#define IPC_CHAN_MASK(n)	(1 << (n))

void ipc_init(uintptr_t _ipc_base);
void ipc_setup_1to1(uint32_t tx_mbox, uint32_t rx_mbox);
void ipc_setup_broadcast(uint32_t tx_mbox);

/* rx_mboxes is a bit mask for the mailboxes to send to */
void ipc_setup_custom(uint32_t tx_mbox, uint32_t rx_mboxes);
void ipc_send(uint32_t tx_mbox, uint32_t *data);
void ipc_wait_for_ack(uint32_t tx_mbox);

/* from_mboxes is a bit mask of the sending mailboxes to wait on */
void ipc_recv(uint32_t rx_mbox, uint32_t from_mboxes, uint32_t *data);

void ipc_recv_all(uint32_t rx_mbox, uint32_t *data);

#endif /* __IPCM_H__ */
