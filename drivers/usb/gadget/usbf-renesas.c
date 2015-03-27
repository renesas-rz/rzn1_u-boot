/*
 * Renesas USB Device/Function driver
 *
 * Copyright (C) 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */
#include <common.h>
#include <command.h>
#include <config.h>
#include <net.h>
#include <malloc.h>
#include <asm/byteorder.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <asm/unaligned.h>
#include <linux/types.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <usb.h>
#include <usb/ci_udc.h>
#include <renesas/rzn1-memory-map.h>
#include "usbf-renesas.h"


static struct usb_endpoint_descriptor ep0_desc = {
	.bLength = sizeof(struct usb_endpoint_descriptor),
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_CONTROL,
};

/*
 * Endpoint zero callbacks
 */
static int usbf_ep0_flush_buffer(
	struct f_regs_ep0 *ep_reg,
	int do_clear)
{
	int res = 100000;

	while (do_clear || !(readl(&ep_reg->status) & D_EP0_IN_EMPTY)) {
		if (do_clear)
			writel(readl(&ep_reg->control) | D_EP0_BCLR,
			       &ep_reg->control);
		while (!(readl(&ep_reg->status) & D_EP0_IN_EMPTY) && res--)
			;
		if (res == 0 && !do_clear) {
			TRACE("%s timeout on buffer clear!\n", __func__);
			res = 1000;
			do_clear++;
			continue;
		}
		break;
	}
	return res;
}

static void usbf_ep0_clear_inak(
	struct f_regs_ep0 *ep_reg)
{
	writel((readl(&ep_reg->control)|D_EP0_INAK_EN) & ~D_EP0_INAK,
	       &ep_reg->control);
}

static void usbf_ep0_clear_onak(
	struct f_regs_ep0 *ep_reg)
{
	writel((readl(&ep_reg->control)) & ~D_EP0_ONAK,
	       &ep_reg->control);
}

static void usbf_ep0_stall(
	struct f_regs_ep0 *ep_reg)
{
	writel(readl(&ep_reg->control) | D_EP0_STL,
	       &ep_reg->control);
}

static int usbf_ep0_enable(
	struct f_endpoint *ep)
{
	struct f_drv *chip = ep->chip;
	struct f_regs_ep0 *ep_reg = &chip->regs->ep0;

	writel(D_EP0_INAK_EN | D_EP0_BCLR, &ep_reg->control);
	writel(D_EP0_SETUP_EN | D_EP0_STG_START_EN | D_EP0_STG_END_EN |
	       D_EP0_STALL_EN | D_EP0_IN_EN | D_EP0_OUT_EN |
	       D_EP0_OUT_OR_EN | D_EP0_OUT_NULL_EN | D_EP0_IN_NAK_EN,
	       &ep_reg->int_enable);
	return 0;
}

static void usbf_ep0_disable(
	struct f_endpoint *ep)
{
	/* TODO */
}

/*
 * This can be called repeatedly until the request is done
 */
static int usbf_ep0_send(
	struct f_endpoint *ep,
	struct f_req *req)
{
	struct f_drv *chip = ep->chip;
	struct f_regs_ep0 *ep_reg = &chip->regs->ep0;
	uint32_t control;
	int res = 0;

	TRACE("%s %s\n", __func__, req == NULL ? "NULL PACKET" : "");
	/* clear the buffer, perhaps */
	usbf_ep0_flush_buffer(ep_reg, 0);
	/* Regardless of whether we have a request or not, we are going to
	 * send a packet anyway, so prepare the control word */
	control = readl(&ep_reg->control);
	control |= D_EP0_DEND;

	if (req) {
		TRACE("%s l %d actual %d\n", __func__,
		      req->req.length, req->req.actual);
		uint32_t *src = req->req.buf + req->req.actual;
		uint32_t maxlen  = req->req.length - req->req.actual;
		uint32_t tosend = maxlen > ep->ep.maxpacket ?
					ep->ep.maxpacket : maxlen;
		uint32_t len = tosend;
		int i;

		for (i = 0; len >= 4; i += 4, len -= 4)
			writel(*src++, &ep_reg->write);

		/* if we have stray bytes, write them off too, and mark the
		 * control registers so it knows only 1,2,3 bytes are valid in
		 * the last write we made */
		if (len) {
			control |= (len << 5);
			writel(*src, &ep_reg->write);
		}
		req->req.actual += tosend;
		TRACE("%s sent %d remains %d\n", __func__, tosend,
		      req->req.length - req->req.actual);
		/* mark it done, if it is */
		req->req.status = req->req.actual == req->req.length ?
					0 : -EINPROGRESS;
		res = req->req.status;
	}
	writel(control, &ep_reg->control);
	return res;
}

/*
 * This can be called repeatedly until the request is done
 */
static int usbf_ep0_recv(
	struct f_endpoint *ep,
	struct f_req *req)
{
	struct f_drv *chip = ep->chip;
	struct f_regs_ep0 *ep_reg = &chip->regs->ep0;
	uint32_t reqlen = readl(&ep_reg->length);
	uint32_t len = reqlen;
	uint32_t *buf  = req->req.buf + req->req.actual;

	TRACE("%s size %d (%d/%d)\n", __func__, len,
	      req->req.actual, req->req.length);
	while (len > 0) {
		int rem = len >= 4 ? 4 : len;
		*buf++ = readl(&ep_reg->read);
		len -= rem;
		req->req.actual += rem;
	}

	if (req->req.actual == req->req.length)
		req->req.status = 0;
	return req->req.status;
}

/*
 * result of setup packet
 */
#define CX_IDLE		0
#define CX_FINISH	1
#define CX_STALL	2

static void usbf_ep0_setup(
	struct f_endpoint *ep,
	struct usb_ctrlrequest *req)
{
	int ret = CX_IDLE;
	struct f_drv *chip = ep->chip;
	struct f_regs_ep0 *ep_reg = &chip->regs->ep0;

	if (req->bRequestType & USB_DIR_IN)
		ep0_desc.bEndpointAddress = USB_DIR_IN;
	else
		ep0_desc.bEndpointAddress = USB_DIR_OUT;

	/* TODO This is mandatory, as for the moment at least, we never get an
	 * interrupt/status flag indicating the speed has changed. And without
	 * a speed change flag, the gadget upper layer is incapable of finding
	 * a valid configuration */
	if (chip->gadget.speed == USB_SPEED_UNKNOWN) {
		debug("%s Forcing speed\n", __func__);
		if ((readl(&chip->regs->status) & D_USB_SPEED_MODE) ==
				D_USB_SPEED_MODE)
			chip->gadget.speed = USB_SPEED_HIGH;
		else
			chip->gadget.speed = USB_SPEED_FULL;
	}

	if ((req->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (req->bRequest) {
		case USB_REQ_SET_CONFIGURATION:
			debug("usbf: set_cfg(%d)\n", req->wValue & 0x00FF);
			if (!(req->wValue & 0x00FF)) {
				chip->state = USB_STATE_ADDRESS;
				writel((uint32_t)chip->addr << 16,
				       &chip->regs->address);
			} else {
				chip->state = USB_STATE_CONFIGURED;
			}
			ret = CX_IDLE;
			break;

		case USB_REQ_SET_ADDRESS:
			debug("usbf: set_addr(0x%04X)\n", req->wValue);
			chip->state = USB_STATE_ADDRESS;
			chip->addr  = req->wValue & 0x7f /*DEVADDR_ADDR_MASK */;
			ret = CX_FINISH;
			writel((uint32_t)chip->addr << 16,
			       &chip->regs->address);
			break;

		case USB_REQ_CLEAR_FEATURE:
			debug("usbf: clr_feature(%d, %d)\n",
			      req->bRequestType & 0x03, req->wValue);
			switch (req->wValue) {
			case 0:    /* [Endpoint] halt */
				/* TODO */
			/*	ep_reset(chip, req->wIndex); */
				printf("endpoint reset ?!?\n");
				ret = CX_FINISH;
				break;
			case 1:    /* [Device] remote wake-up */
			case 2:    /* [Device] test mode */
			default:
				ret = CX_STALL;
				break;
			}
			break;

		case USB_REQ_SET_FEATURE:
			debug("usbf: set_feature(%d, %d)\n",
			      req->wValue, req->wIndex & 0xf);
			switch (req->wValue) {
			case 0:    /* Endpoint Halt */
				ret = CX_FINISH;
				/* TODO */
			/*	id = req->wIndex & 0xf; */
				break;
			case 1:    /* Remote Wakeup */
			case 2:    /* Test Mode */
			default:
				ret = CX_STALL;
				break;
			}
			break;
		case USB_REQ_GET_STATUS:
			debug("usbf: get_status\n");
			break;
		case USB_REQ_SET_DESCRIPTOR:
			debug("usbf: set_descriptor\n");
			break;
		case USB_REQ_SYNCH_FRAME:
			debug("usbf: sync frame\n");
			break;
		}
	} /* if ((req->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) */

	if (ret == CX_IDLE && chip->driver->setup) {
		if (chip->driver->setup(&chip->gadget, req) < 0)
			ret = CX_STALL;
		else
			ret = CX_FINISH;
	}

	switch (ret) {
	case CX_FINISH:
		break;
	case CX_STALL:
		usbf_ep0_stall(ep_reg);
		debug("usbf: cx_stall!\n");
		break;
	case CX_IDLE:
		TRACE("usbf: cx_idle?\n");
	default:
		break;
	}
}

static void usbf_ep0_process_current_io(
	struct f_endpoint *ep,
	int send_null)
{
	if (!list_empty(&ep->queue)) {
		int res = 0;
		struct f_req *req = list_first_entry(&ep->queue,
					struct f_req, queue);
		ep->null_sent = 0;
		if (req->process)
			res = req->process(ep, req);
		if (res == 0) {
			if (req->req.complete)
				req->req.complete(&ep->ep, &req->req);

			if (req->req.status == 0)
				list_del_init(&req->queue);
		}
	} else if (send_null) {
		if (!ep->null_sent)
			usbf_ep0_send(ep, NULL);
		ep->null_sent = 1;
	}
}

static void usbf_ep0_interrupt(
	struct f_endpoint *ep)
{
	struct f_regs_ep0 *ep_reg = &ep->chip->regs->ep0;
	uint32_t status = readl(&ep_reg->status);

	TRACE("%s status %08x control %08x\n", __func__, status,
	      readl(&ep_reg->control));
	if (status & D_EP0_STALL_INT) {
		writel(~D_EP0_STALL_INT, &ep_reg->status);
	}
	if (status & D_EP0_IN_NAK_INT)
		writel(~D_EP0_IN_NAK_INT, &ep_reg->status);
	if (status & D_EP0_STG_END_INT)
		writel(~D_EP0_STG_END_INT, &ep_reg->status);

	if (status & D_EP0_OUT_INT) {
		TRACE("%s OUT (length %d) direction %s\n", __func__,
		      ep->chip->regs->ep0.length,
		      ep->desc->bEndpointAddress & USB_DIR_IN ?
				"input" : "output");
		writel(~D_EP0_OUT_INT, &ep_reg->status);

		usbf_ep0_process_current_io(ep, 0);
	}

	if (status & D_EP0_SETUP_INT) {
		struct usb_ctrlrequest *req =
			(struct usb_ctrlrequest *)ep->setup;

		writel(~D_EP0_SETUP_INT, &ep_reg->status);

		ep->setup[0] = readl(&ep->chip->regs->setup_data0);
		ep->setup[1] = readl(&ep->chip->regs->setup_data1);

		TRACE("%s SETUP %08x %08x dir %s\n", __func__,
		      ep->setup[0], ep->setup[1],
		      req->bRequestType & USB_DIR_IN ? "input" : "output");

		if (req->bRequestType & USB_DIR_IN) {
			usbf_ep0_flush_buffer(ep_reg, 1);
			usbf_ep0_clear_inak(ep_reg);
		} else {
			usbf_ep0_clear_onak(ep_reg);
		}
		usbf_ep0_setup(ep, req);
		ep->null_sent = 0;
		usbf_ep0_process_current_io(ep, 0);
	}
	if (status & D_EP0_STG_START_INT) {
		struct usb_ctrlrequest *req =
			(struct usb_ctrlrequest *)ep->setup;

		writel(~D_EP0_STG_START_INT, &ep_reg->status);

		TRACE("%s START %08x %08x (empty: %s)\n", __func__,
		      ep->setup[0], ep->setup[1],
		      status & D_EP0_IN_EMPTY ? "empty" : "NOT empty");
		if (req->bRequestType & USB_DIR_IN) {
			usbf_ep0_clear_onak(ep_reg);
		} else {
			usbf_ep0_flush_buffer(ep_reg, 1);
			usbf_ep0_clear_inak(ep_reg);
		}
		usbf_ep0_process_current_io(ep, 1);
	}
	if (status & D_EP0_IN_INT) {
		TRACE("%s IN\n", __func__);
		writel(~D_EP0_IN_INT, &ep_reg->status);

		usbf_ep0_process_current_io(ep, 1);
	}
	if (status & D_EP0_OUT_NULL_INT) {
		TRACE("%s OUT_NULL (length %d)\n", __func__,
		      ep->chip->regs->ep0.length);
		writel(~D_EP0_OUT_NULL_INT, &ep_reg->status);
	}
}

static const struct f_endpoint_drv usbf_ep0_callbacks = {
	.enable = usbf_ep0_enable,
	.disable = usbf_ep0_disable,
	.set_maxpacket = NULL,
	.recv = usbf_ep0_recv,
	.send = usbf_ep0_send,
	.interrupt = usbf_ep0_interrupt,
};

/*
 * activate/deactivate link with host.
 */
static void pullup(struct f_drv *chip, int is_on)
{
	struct f_regs *regs = chip->regs;

	if (is_on) {
		if (!chip->pullup) {
			chip->state = USB_STATE_POWERED;
			chip->pullup = 1;

			writel((readl(&regs->control) & ~D_USB_CONNECTB) |
				D_USB_PUE2, &regs->control);
		}
	} else {
		chip->state = USB_STATE_NOTATTACHED;
		chip->pullup = 0;
		chip->addr = 0;
		writel((readl(&regs->control) & ~D_USB_PUE2) |
			D_USB_CONNECTB, &regs->control);
	}
}

static int f_pullup(struct usb_gadget *_gadget, int is_on)
{
	struct f_drv *chip = container_of(_gadget, struct f_drv, gadget);

	debug("%s: pullup=%d\n", __func__, is_on);

	pullup(chip, is_on);

	return 0;
}

static struct usb_gadget_ops f_gadget_ops = {
	.pullup = f_pullup,
};

/*
 * USB Gadget Layer
 */
static int usbf_ep_enable(
	struct usb_ep *_ep, const struct usb_endpoint_descriptor *desc)
{
	struct f_endpoint *ep = container_of(_ep, struct f_endpoint, ep);
	struct f_drv *chip = ep->chip;
	struct f_regs *regs = chip->regs;
	int irq_state;

	TRACE("%s(%d) desctype %d max pktsize %d\n", __func__, ep->id,
	      desc->bDescriptorType,
	      le16_to_cpu(desc->wMaxPacketSize));
	if (!_ep || !desc || desc->bDescriptorType != USB_DT_ENDPOINT) {
		printf("%s: bad ep or descriptor\n", __func__);
		return -EINVAL;
	}

	ep->desc = desc;
	ep->ep.maxpacket = ep->id == 0 ?
		CFG_EP0_MAX_PACKET_SIZE : CFG_EPX_MAX_PACKET_SIZE;

	/* set max packet here */

	irq_state = disable_interrupts();

	/* enable interrupts for this endpoint */
	writel(readl(&regs->int_enable) | (D_USB_EP0_EN << ep->id),
	       &regs->int_enable);

	if (ep->drv->enable)
		ep->drv->enable(ep);
	if (ep->drv->set_maxpacket)
		ep->drv->set_maxpacket(ep);
	ep->stopped = 0;

	if (irq_state)
		enable_interrupts();
	return 0;
}

static int usbf_ep_disable(struct usb_ep *_ep)
{
	struct f_endpoint *ep = container_of(_ep, struct f_endpoint, ep);

	if (ep->drv->disable)
		ep->drv->disable(ep);
	ep->desc = NULL;
	ep->stopped = 1;
	return 0;
}

static struct usb_request *usbf_ep_alloc_request(
	struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct f_req *req = calloc(1, sizeof(*req));

	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

static void usbf_ep_free_request(
	struct usb_ep *_ep, struct usb_request *_req)
{
	struct f_req *req;

	req = container_of(_req, struct f_req, req);
	free(req);
}

static int usbf_ep_queue(
	struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct f_endpoint *ep = container_of(_ep, struct f_endpoint, ep);
	struct f_drv *chip = ep->chip;
	struct f_req *req;

	req = container_of(_req, struct f_req, req);
	if (!_req || !_req->complete || !_req->buf ||
	    !list_empty(&req->queue)) {
		printf("%s: invalid request to ep%d\n", __func__, ep->id);
		return -EINVAL;
	}

	if (!chip || chip->state == USB_STATE_SUSPENDED) {
		printf("%s: request while chip suspended\n", __func__);
		return -EINVAL;
	}

	req->req.actual = 0;
	req->req.status = -EINPROGRESS;
	if (ep->desc->bEndpointAddress == USB_DIR_IN)
		req->process = ep->drv->send;
	else
		req->process = ep->drv->recv;

	if (ep->id)
		TRACE("%s %d, maxpacket %d len %d %s\n", __func__, ep->id,
		      ep->ep.maxpacket, req->req.length,
		      ep->desc->bEndpointAddress == USB_DIR_IN ?
				"input" : "output");
	if (req->req.length == 0) {
		req->req.status = 0;
		if (req->req.complete)
			req->req.complete(&ep->ep, &req->req);
		return 0;
	}
	list_add_tail(&req->queue, &ep->queue);

	return 0;
}

static int usbf_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct f_endpoint *ep = container_of(_ep, struct f_endpoint, ep);
	struct f_req *req;

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry(req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req)
		return -EINVAL;

	/* remove the request */
	list_del_init(&req->queue);

	/* update status & invoke complete callback */
	if (req->req.status == -EINPROGRESS) {
		req->req.status = -ECONNRESET;
		if (req->req.complete)
			req->req.complete(_ep, &req->req);
	}

	return 0;
}

static int usbf_ep_halt(struct usb_ep *_ep, int halt)
{
	struct f_endpoint *ep = container_of(_ep, struct f_endpoint, ep);
	int ret = -1;

	debug("%s: ep%d halt=%d\n", __func__, ep->id, halt);
	if (ep->drv->halt)
		ep->drv->halt(ep, halt);
	return ret;
}

static struct usb_ep_ops ci_ep_ops = {
	.enable         = usbf_ep_enable,
	.disable        = usbf_ep_disable,
	.queue          = usbf_ep_queue,
	.dequeue        = usbf_ep_dequeue,
	.set_halt       = usbf_ep_halt,
	.alloc_request  = usbf_ep_alloc_request,
	.free_request   = usbf_ep_free_request,
};

static struct f_drv controller = {
	.regs = (void __iomem *)RZN1_USB_DEV_BASE,
	.gadget = {
		.name = "rzn1_usbf",
		.ops = &f_gadget_ops,
		.ep0 = &controller.ep[0].ep,
		.speed = USB_SPEED_HIGH, /* USB_SPEED_UNKNOWN */
		.is_dualspeed = 1,
		.is_otg = 0,
		.is_a_peripheral = 1,
		.b_hnp_enable = 0,
		.a_hnp_support = 0,
		.a_alt_hnp_support = 0,
	},
};

/*
	There are two interrupt handlers for the USB function block
 */

static void usbf_interrupt_epc(void)
{
	struct f_drv *chip = &controller;
	struct f_regs *regs = chip->regs;
	uint32_t status = readl(&regs->int_status);
	uint32_t raw_status = status;
	int i;

	if (!status)
		return;

	if (status & D_USB_USB_RST_INT) {
		status &= ~D_USB_USB_RST_INT;
		writel(status, &regs->int_status);
		/* TODO clear endpoints buffers: EPx_CONTROL |= D_EPx_BCLR */
		if (chip->gadget.speed != USB_SPEED_UNKNOWN) {
			chip->gadget.speed = USB_SPEED_UNKNOWN;
			chip->driver->disconnect(&chip->gadget);
		}
	}
	if (status & D_USB_SPEED_MODE_INT) {
		debug("**** %s speed change\n", __func__);
		status &= ~D_USB_SPEED_MODE_INT;
		writel(status, &regs->int_status);
		if ((readl(&chip->regs->status) & D_USB_SPEED_MODE) ==
				D_USB_SPEED_MODE)
			chip->gadget.speed = USB_SPEED_HIGH;
		else
			chip->gadget.speed = USB_SPEED_FULL;
	}
	if (status & D_USB_SPND_INT) {
		status &= ~D_USB_SPND_INT;
		writel(status, &regs->int_status);
	}
	if (status & D_USB_RSUM_INT) {
		status &= ~D_USB_RSUM_INT;
		writel(status, &regs->int_status);
	}

	for (i = 0; i < CFG_NUM_ENDPOINTS; i++) {
		struct f_endpoint *ep = &chip->ep[i];
		/* TODO, new callback for reset? */
		if (raw_status & D_USB_USB_RST_INT) {
			struct f_regs_ep *ep_reg = ep->id == 0 ?
				(struct f_regs_ep *)&regs->ep0 :
				&regs->ep[ep->id - 1];
			writel(readl(&ep_reg->control) | D_EP0_BCLR,
			       &ep_reg->control);
		}
		/* speed change notification for endpoints */
		if (raw_status & D_USB_SPEED_MODE_INT && ep->drv->set_maxpacket)
			ep->drv->set_maxpacket(ep);
		/* Interrupt notification */
		if (status & (D_USB_EP0_INT << i) && ep->drv->interrupt)
			ep->drv->interrupt(ep);
	}
}

static void usbf_interrupt(void)
{
	struct f_drv *chip = &controller;
	struct f_regs *regs = chip->regs;
	uint32_t sysbint = readl(&regs->sysbint);

	if ((sysbint & D_SYS_VBUS_INT) == D_SYS_VBUS_INT) {
		uint32_t state = readl(&regs->control);
		/* Interrupt factor clear */
		sysbint |= D_SYS_VBUS_INT;
		writel(sysbint, &regs->sysbint);
		if ((readl(&regs->epctr) & D_SYS_VBUS_LEVEL) ==
				D_SYS_VBUS_LEVEL) {
			debug("%s plugged in\n", __func__);
			/* Enable the USB signal to the Function PHY */
			state      &= ~D_USB_CONNECTB;
			/* D + signal Pull-up */
			state      |=  D_USB_PUE2;
		} else {
			debug("%s plugged out\n", __func__);
			/* disable the USB signal to the Function PHY */
			state      |=  D_USB_CONNECTB;
			/* Do not Pull-up the D +  */
			state      &= ~D_USB_PUE2;
			/* Disable the endpoint 0 */
			state      &= ~D_USB_DEFAULT;
			/* disable the other endpoint 0 */
			state      &= ~D_USB_CONF;
		}
		writel(state, &regs->control);
	}
}

int usb_gadget_handle_interrupts(void)
{
	usbf_interrupt();
	usbf_interrupt_epc();

	return 0;
}

#ifdef CONFIG_USE_IRQ
/* DEBUG hack here, ignore for the moment */
struct pt_regs;
int rzn1_set_irq_handler(
	int irqn,
	int (*handler)(int irqn, struct pt_regs *pt_regs, void *param),
	void *param);

static int irq_handler(int irqn, struct pt_regs *pt_regs, void *param)
{
	return 0;
}
#endif

int usb_gadget_register_driver(struct usb_gadget_driver *driver)
{
	int ret, i;
	struct f_drv *chip = &controller;
	static const char *ep_name[16] = {
		"ep0", "ep1", "ep2", "ep3", "ep4", "ep5", "ep6", "ep7",
		"ep8", "ep9", "ep10", "ep11", "ep12", "ep13", "ep14", "ep15",
	};

	if (!driver    || !driver->bind || !driver->setup) {
		printf("%s: bad parameter.\n", __func__);
		return -EINVAL;
	}
	INIT_LIST_HEAD(&chip->gadget.ep_list);

	for (i = 0; i < CFG_NUM_ENDPOINTS; ++i) {
		struct f_endpoint init = {
			.ep = {
				.name = ep_name[i],
				.ops = &ci_ep_ops,
				.maxpacket = CFG_EPX_MAX_PACKET_SIZE,
			},
			.id = i,
			.stopped = 1,
			.chip = chip,
		};
		struct f_endpoint *ep = chip->ep + i;

		*ep = init;

		INIT_LIST_HEAD(&ep->queue);

		if (ep->id == 0) {
			ep->drv = &usbf_ep0_callbacks;
			ep->ep.maxpacket = CFG_EP0_MAX_PACKET_SIZE;
		} else {
#ifdef CONFIG_USBF_RENESAS_FULL
			if (!usbf_epx_init(ep))
				list_add_tail(&ep->ep.ep_list,
					      &chip->gadget.ep_list);
#endif
		}
	}
#ifdef CONFIG_USE_IRQ
	rzn1_set_irq_handler(32 + RZN1_IRQ_USBF_EPC, irq_handler, NULL);
	rzn1_set_irq_handler(32 + RZN1_IRQ_USBF, irq_handler, NULL);

	writel(D_SYS_VBUS_INTEN, &chip->regs->sysbinten);
#endif

	/* get out of reset */
	writel(readl(&chip->regs->epctr) &
	       ~((1 << 0)), &chip->regs->epctr);
	writel(readl(&chip->regs->epctr) &
	       ~((1 << 2)), &chip->regs->epctr);

	writel(D_USB_INT_SEL | D_USB_SOF_RCV,
	       &chip->regs->control);
	/* Enable mode change interrupts, and EP0 */
	writel(D_USB_USB_RST_EN | D_USB_SPEED_MODE_EN | D_USB_EP0_EN,
	       &chip->regs->int_enable);
	/* Endpoint zero is always enabled anyway */
	usbf_ep_enable(&chip->ep[0].ep, &ep0_desc);

	ret = driver->bind(&chip->gadget);
	if (ret) {
		debug("%s: driver->bind() returned %d\n", __func__, ret);
		return ret;
	}
	chip->driver = driver;

	return ret;
}

int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
	struct f_drv *chip = &controller;

	/* remove the pullup, apparently putting the controller
	 * in reset doesn't do it! */
	pullup(chip, 0);
	/* get back into reset */
	writel(readl(&chip->regs->epctr) |
	       ((1 << 0)), &chip->regs->epctr);
	writel(readl(&chip->regs->epctr) |
	       ((1 << 2)), &chip->regs->epctr);

	return 0;
}
