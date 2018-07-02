/*
 * Renesas USB Device/Function driver
 *
 * Copyright (C) 2015 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */
#include <common.h>
#include <config.h>
#include <malloc.h>
#include <linux/errno.h>
#include <asm/io.h>
#include <linux/types.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <usb.h>
#include "usbf-renesas.h"

#define spin_lock_irq(lock) do { } while (0)
#define spin_unlock_irq(lock) do { } while (0)

#define list_first_entry_or_null(ptr, type, member) ({ \
	struct list_head *head__ = (ptr); \
	struct list_head *pos__ = READ_ONCE(head__->next); \
	pos__ != head__ ? list_entry(pos__, type, member) : NULL; \
})

void usb_gadget_set_state(struct usb_gadget *gadget,
		enum usb_device_state state)
{
	gadget->state = state;
}

void usb_gadget_giveback_request(struct usb_ep *ep,
		struct usb_request *req)
{
	req->complete(ep, req);
}

static struct usb_endpoint_descriptor ep0_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_CONTROL,
	.wMaxPacketSize = CFG_EP0_MAX_PACKET_SIZE,
};

/*
 * Endpoint 0 callbacks
 */

static int usbf_ep0_flush_buffer(
	struct f_regs_ep0 *ep_reg,
	uint32_t bits)
{
	int res = 100000;

	if ((readl(&ep_reg->status) & bits) == bits)
		return res;

	writel(readl(&ep_reg->control) | D_EP0_BCLR, &ep_reg->control);

	while (res-- && ((readl(&ep_reg->status) & bits) != bits))
		;

	if (!res)
		printk("%s timeout on buffer clear!\n", __func__);

	return res;
}

static void usbf_ep0_clear_inak(
	struct f_regs_ep0 *ep_reg)
{
	writel((readl(&ep_reg->control) | D_EP0_INAK_EN) & ~D_EP0_INAK,
	       &ep_reg->control);
}

static void usbf_ep0_clear_onak(
	struct f_regs_ep0 *ep_reg)
{
	writel((readl(&ep_reg->control)) & ~D_EP0_ONAK, &ep_reg->control);
}

static void usbf_ep0_stall(
	struct f_regs_ep0 *ep_reg)
{
	writel(readl(&ep_reg->control) | D_EP0_STL, &ep_reg->control);
}

static int usbf_ep0_enable(
	struct f_endpoint *ep)
{
	struct f_drv *chip = ep->chip;
	struct f_regs_ep0 *ep_reg = &chip->regs->ep0;

	writel(D_EP0_INAK_EN | D_EP0_BCLR, &ep_reg->control);
	writel(D_EP0_SETUP_EN | D_EP0_STG_START_EN |
	       D_EP0_OUT_EN,
	       &ep_reg->int_enable);
	return 0;
}

static void usbf_ep0_reset(
	struct f_endpoint *ep)
{
	struct f_drv *chip = ep->chip;
	struct f_regs_ep0 *ep_reg = &chip->regs->ep0;

	writel(readl(&ep_reg->control) | D_EP0_BCLR, &ep_reg->control);
}

static int usbf_ep0_send1(
	struct f_endpoint *ep,
	uint32_t *src,
	int reqlen)
{
	struct f_drv *chip = ep->chip;
	struct f_regs_ep0 *ep_reg = &chip->regs->ep0;
	uint32_t control;
	int w, len;
	int pkt_words = reqlen / sizeof(*src);

	/* Wait until there is space to write the pkt */
	while ((readl(&ep_reg->status) & D_EP0_IN_EMPTY) == 0)
		;

	/* Note, we don't care about endianness here, as the IP
	 * and the core will have the same layout anyway, so we
	 * can happily ignore it */
	for (w = 0; w < pkt_words; w++)
		writel(*src++, &ep_reg->write);

	control = readl(&ep_reg->control);

	/* if we have stray bytes, write them off too, and mark the
	 * control registers so it knows only 1,2,3 bytes are valid in
	 * the last write we made */
	len = reqlen & (sizeof(*src) - 1);
	if (len) {
		writel(*src, &ep_reg->write);
		control |= (len << 5);
	}

	writel(control | D_EP0_DEND, &ep_reg->control);

	return 0;
}

static int usbf_ep0_send(
	struct f_endpoint *ep,
	struct f_req *req)
{
	struct f_drv *chip = ep->chip;
	struct f_regs_ep0 *ep_reg = &chip->regs->ep0;

	/* Special handling for internally generated NULL packets */
	if (!req) {
		writel(readl(&ep_reg->control) | D_EP0_DEND, &ep_reg->control);
		return 0;
	}

	if (req->req.length) {
		void *src = req->req.buf;
		int bytes = req->req.length;
		int maxpkt_bytes = ep->ep.maxpacket;
		int ret;

		while (bytes > 0) {
			int pkt_bytes = min(bytes, maxpkt_bytes);

			ret = usbf_ep0_send1(ep, src, pkt_bytes);
			if (ret < 0) {
				req->req.status = ret;
				return ret;
			}

			bytes -= pkt_bytes;
			src += pkt_bytes;
		}
		req->req.actual = req->req.length;
		req->req.status = 0;
	}

	/* UDC asking for a ZLP to follow? */
	if (req->req.length == 0 || req->req.zero)
		req->req.status = usbf_ep0_send1(ep, NULL, 0);

	TRACERQ(req, "%s[%d][%3d] sent %d\n", __func__,
		ep->id, req->seq, req->req.length);

	return req->req.status;
}

/*
 * This can be called repeatedly until the request is done
 */
static int usbf_ep0_recv(
	struct f_endpoint *ep,
	struct f_req *req)
{
	return req->req.status;
}

static void usbf_ep0_out_isr(
	struct f_endpoint *ep,
	struct f_req *req)
{
	struct f_regs_ep0 *ep_reg = &ep->chip->regs->ep0;
	uint32_t reqlen = readl(&ep_reg->length);
	int len = reqlen;
	uint32_t *buf  = req->req.buf + req->req.actual;

	TRACEEP(ep, "%s[%3d] size %d (%d/%d)\n", __func__, req->seq, len,
	      req->req.actual, req->req.length);
	while (len > 0) {
		*buf++ = readl(&ep_reg->read);
		len -= 4;
	}
	req->req.actual += reqlen;

	if (req->req.actual == req->req.length)
		req->req.status = 0;
}


/*
 * result of setup packet
 */
#define CX_IDLE		0
#define CX_FINISH	1
#define CX_STALL	2

static void usbf_ep0_setup(
	struct f_endpoint *ep,
	struct usb_ctrlrequest *ctrl)
{
	int ret = CX_IDLE;
	struct f_drv *chip = ep->chip;
	struct f_regs_ep0 *ep_reg = &chip->regs->ep0;
	uint16_t value = ctrl->wValue & 0xff;

	if (ctrl->bRequestType & USB_DIR_IN)
		ep->desc->bEndpointAddress = USB_DIR_IN;
	else
		ep->desc->bEndpointAddress = USB_DIR_OUT;

	/* TODO:
	 * This is mandatory, as for the moment at least, we never get an
	 * interrupt/status flag indicating the speed has changed. And without
	 * a speed change flag, the gadget upper layer is incapable of finding
	 * a valid configuration */
	if (readl(&chip->regs->status) & D_USB_SPEED_MODE)
		chip->gadget.speed = USB_SPEED_HIGH;
	else
		chip->gadget.speed = USB_SPEED_FULL;

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) {
		switch (ctrl->bRequest) {
		case USB_REQ_SET_CONFIGURATION:
			TRACEEP(ep, "usbf: set_cfg(%d)\n", value);
			if (!value) {
				/* Disable all endpoints other than EP0 */
				writel(readl(&chip->regs->control) & ~D_USB_CONF,
				       &chip->regs->control);

				usb_gadget_set_state(&chip->gadget, USB_STATE_ADDRESS);
			} else {
				/* Enable all endpoints */
				writel(readl(&chip->regs->control) | D_USB_CONF,
				       &chip->regs->control);

				usb_gadget_set_state(&chip->gadget, USB_STATE_CONFIGURED);
			}
			ret = CX_IDLE;
			break;

		case USB_REQ_SET_ADDRESS:
			TRACEEP(ep, "usbf: set_addr(0x%04X)\n", ctrl->wValue);
			writel(value << 16, &chip->regs->address);
			usb_gadget_set_state(&chip->gadget, USB_STATE_ADDRESS);
			ret = CX_FINISH;
			break;

		case USB_REQ_CLEAR_FEATURE:
			TRACEEP(ep, "usbf: clr_feature(%d, %d)\n",
			      ctrl->bRequestType & 0x03, ctrl->wValue);
			switch (ctrl->wValue) {
			case 0:    /* [Endpoint] halt */
				/* TODO ? */
			/*	ep_reset(chip, ctrl->wIndex); */
				TRACEEP(ep, "endpoint reset ?!?\n");
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
			TRACEEP(ep, "usbf: set_feature(%d, %d)\n",
			      ctrl->wValue, ctrl->wIndex & 0xf);
			switch (ctrl->wValue) {
			case 0:    /* Endpoint Halt */
				ret = CX_FINISH;
				/* TODO */
			/*	id = ctrl->wIndex & 0xf; */
				break;
			case 1:    /* Remote Wakeup */
			case 2:    /* Test Mode */
			default:
				ret = CX_STALL;
				break;
			}
			break;
		case USB_REQ_GET_STATUS:
			TRACEEP(ep, "usbf: get_status(%d, %d, type %d)\n",
			      ctrl->wValue, ctrl->wIndex,
			      ctrl->bRequestType & USB_RECIP_MASK);
			chip->setup[0] = 0;
			switch (ctrl->bRequestType & USB_RECIP_MASK) {
			case USB_RECIP_DEVICE:
				chip->setup[0] = 1 << USB_DEVICE_SELF_POWERED;
				break;
			}
			/* mark it as static, don't 'free' it */
			chip->setup_reply.req.complete = NULL;
			chip->setup_reply.req.buf = &chip->setup;
			chip->setup_reply.req.length = 2;
			usb_ep_queue(&ep->ep, &chip->setup_reply.req, 0);
			ret = CX_FINISH;
			break;
		case USB_REQ_SET_DESCRIPTOR:
			TRACEEP(ep, "usbf: set_descriptor\n");
			break;
		}
	} /* if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) */

	if (!chip->driver) {
		dev_warn(chip->dev, "Spurious SETUP");
		ret = CX_STALL;
	} else if (ret == CX_IDLE && chip->driver->setup) {
		if (chip->driver->setup(&chip->gadget, ctrl) < 0)
			ret = CX_STALL;
		else
			ret = CX_FINISH;
	}

	switch (ret) {
	case CX_FINISH:
		break;
	case CX_STALL:
		usbf_ep0_stall(ep_reg);
		TRACEEP(ep, "usbf: cx_stall!\n");
		break;
	case CX_IDLE:
		TRACEEP(ep, "usbf: cx_idle?\n");
	default:
		break;
	}
}

static int usbf_req_is_control_no_data(struct usb_ctrlrequest *ctrl)
{
	return (ctrl->wLength == 0);
}

static int usbf_req_is_control_read(struct usb_ctrlrequest *ctrl)
{
	if (ctrl->wLength && (ctrl->bRequestType & USB_DIR_IN))
		return 1;
	return 0;
}

static int usbf_req_is_control_write(struct usb_ctrlrequest *ctrl)
{
	if (ctrl->wLength && !(ctrl->bRequestType & USB_DIR_IN))
		return 1;
	return 0;
}

static void usbf_ep0_interrupt(
	struct f_endpoint *ep)
{
	struct f_regs_ep0 *ep_reg = &ep->chip->regs->ep0;
	struct f_drv *chip = ep->chip;
	struct usb_ctrlrequest *ctrl = (struct usb_ctrlrequest *)chip->setup;

/*	TRACE("%s status %08x control %08x\n", __func__, ep->status,
		readl(&ep_reg->control)); */

	if (ep->status & D_EP0_OUT_INT) {
		struct f_req *req;

		spin_lock_irq(&ep->lock);
		req = list_first_entry_or_null(&ep->queue, struct f_req, queue);
		spin_unlock_irq(&ep->lock);

		if (req)
			usbf_ep0_out_isr(ep, req);
	}

	if (ep->status & D_EP0_SETUP_INT) {
		chip->setup[0] = readl(&chip->regs->setup_data0);
		chip->setup[1] = readl(&chip->regs->setup_data1);

		TRACEEP(ep, "%s SETUP %08x %08x dir %s len:%d\n", __func__,
		      chip->setup[0], chip->setup[1],
		      (ctrl->bRequestType & USB_DIR_IN) ? "input" : "output",
		      readl(&ep->chip->regs->ep0.length));

		if (usbf_req_is_control_write(ctrl)) {
			usbf_ep0_clear_onak(ep_reg);
		}
		if (usbf_req_is_control_read(ctrl)) {
			usbf_ep0_flush_buffer(ep_reg, D_EP0_IN_EMPTY);
			usbf_ep0_clear_inak(ep_reg);
		}

		usbf_ep0_setup(ep, ctrl);
	}
	if (ep->status & D_EP0_STG_START_INT) {
		TRACEEP(ep, "%s START %08x %08x (empty: %s)\n", __func__,
		      chip->setup[0], chip->setup[1],
		      (ep->status & D_EP0_IN_EMPTY) ?
				"IN empty" : "IN NOT empty");

		if (usbf_req_is_control_read(ctrl)) {
			usbf_ep0_clear_onak(ep_reg);
		}
		if (usbf_req_is_control_write(ctrl)) {
			usbf_ep0_flush_buffer(ep_reg, D_EP0_OUT_EMPTY);
			usbf_ep0_clear_inak(ep_reg);
		}
		if (usbf_req_is_control_no_data(ctrl)) {
			usbf_ep0_flush_buffer(ep_reg, D_EP0_IN_EMPTY);
			usbf_ep0_clear_inak(ep_reg);
		}

		/* TODO, we should send a NULL packet for Control-No-Data, but read a NULL packet for Control-Read */
		usbf_ep0_send(ep, NULL);
	}

	ep->status = 0;
}

static const struct f_endpoint_drv usbf_ep0_callbacks = {
	.enable = usbf_ep0_enable,
	.recv = usbf_ep0_recv,
	.send = usbf_ep0_send,
	.interrupt = usbf_ep0_interrupt,
	.reset = usbf_ep0_reset,
};

int usbf_ep0_init(struct f_endpoint *ep)
{
	struct f_regs_ep0 *ep_reg = &ep->chip->regs->ep0;

	ep->drv = &usbf_ep0_callbacks;
	ep->ep.maxpacket = CFG_EP0_MAX_PACKET_SIZE;

	usbf_ep0_flush_buffer(ep_reg, D_EP0_OUT_EMPTY | D_EP0_IN_EMPTY);

	return 0;
}

/*
 * activate/deactivate link with host.
 */
static void pullup(struct f_drv *chip, int is_on)
{
	struct f_regs *regs = chip->regs;

	if (is_on) {
		if (!chip->pullup) {
			chip->pullup = 1;
			writel((readl(&regs->control) & ~D_USB_CONNECTB) |
				D_USB_PUE2, &regs->control);
			usb_gadget_set_state(&chip->gadget, USB_STATE_POWERED);
		}
	} else {
		chip->pullup = 0;
		writel((readl(&regs->control) & ~D_USB_PUE2) |
			D_USB_CONNECTB, &regs->control);
		usb_gadget_set_state(&chip->gadget, USB_STATE_NOTATTACHED);
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
	struct usb_ep *_ep,
	const struct usb_endpoint_descriptor *desc)
{
	struct f_endpoint *ep = container_of(_ep, struct f_endpoint, ep);
	struct f_drv *chip = ep->chip;
	struct f_regs *regs = chip->regs;
	unsigned long flags;

	TRACE("%s[%d] desctype %d max pktsize %d\n", __func__, ep->id,
	      desc->bDescriptorType,
	      usb_endpoint_maxp(desc));
	if (!_ep || !desc || desc->bDescriptorType != USB_DT_ENDPOINT) {
		TRACE("%s: bad ep or descriptor\n", __func__);
		return -EINVAL;
	}

	/* it might appear as we nuke the const here, but in this case,
	 * we just need the ep0 to be able to change the endpoint direction,
	 * and we know it does that on a non-const copy of its descriptor,
	 * while the other endpoints don't touch it anyway */
	ep->desc = (struct usb_endpoint_descriptor *)desc;
	ep->ep.maxpacket = usb_endpoint_maxp(desc);

	if (ep->drv->enable)
		ep->drv->enable(ep);
	if (ep->drv->set_maxpacket)
		ep->drv->set_maxpacket(ep);
	ep->disabled = 0;

	/* enable interrupts for this endpoint */
	spin_lock_irqsave(&chip->lock, flags);
	writel(readl(&regs->int_enable) | (D_USB_EP0_EN << ep->id),
	       &regs->int_enable);
	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int usbf_ep_disable(struct usb_ep *_ep)
{
	struct f_endpoint *ep = container_of(_ep, struct f_endpoint, ep);
	struct f_drv *chip = ep->chip;
	struct f_regs *regs = chip->regs;
	unsigned long flags;

	TRACE("%s(%d)\n", __func__, ep->id);

	/* disable interrupts for this endpoint */
	spin_lock_irqsave(&chip->lock, flags);
	writel(readl(&regs->int_enable) & ~(D_USB_EP0_EN << ep->id),
	       &regs->int_enable);
	spin_unlock_irqrestore(&chip->lock, flags);
	if (ep->drv->disable)
		ep->drv->disable(ep);
	ep->desc = NULL;
	ep->disabled = 1;
	return 0;
}

static struct usb_request *usbf_ep_alloc_request(
	struct usb_ep *_ep, gfp_t gfp_flags)
{
	struct f_req *req = kzalloc(sizeof(*req), gfp_flags);

	if (!req)
		return NULL;

	INIT_LIST_HEAD(&req->queue);

	return &req->req;
}

static void usbf_ep_free_request(
	struct usb_ep *_ep, struct usb_request *_req)
{
	struct f_req *req = container_of(_req, struct f_req, req);

	spin_lock_irq(&ep->lock);
	list_del_init(&req->queue);
	spin_unlock_irq(&ep->lock);
	kfree(req);
}

static int usbf_ep_queue(
	struct usb_ep *_ep, struct usb_request *_req, gfp_t gfp_flags)
{
	struct f_endpoint *ep = container_of(_ep, struct f_endpoint, ep);
	struct f_drv *chip = ep->chip;
	struct f_req *req = container_of(_req, struct f_req, req);
	int was_empty = list_empty(&ep->queue);

	if (!_req || !_req->buf) {
		TRACE("%s: invalid request to ep%d\n", __func__, ep->id);
		return -EINVAL;
	}

	if (!chip || chip->state == USB_STATE_SUSPENDED) {
		TRACE("%s: request while chip suspended\n", __func__);
		return -EINVAL;
	}
	req->trace = 0;
	req->req.actual = 0;
	req->req.status = -EINPROGRESS;
	req->seq = ep->seq++; /* debug */
	req->to_host = usb_endpoint_dir_in(ep->desc);
	if (req->to_host)
		req->process = ep->drv->send;
	else
		req->process = ep->drv->recv;

	if (ep->id)
		TRACE("%s[%d], len %d %s\n", __func__, ep->id,
		      req->req.length,
		      req->to_host ? "input" : "output");
	if (req->req.length == 0) {
		req->req.status = 0;
		usb_gadget_giveback_request(&ep->ep, &req->req);
		return 0;
	}

	spin_lock_irq(&ep->lock);
	list_add_tail(&req->queue, &ep->queue);
	spin_unlock_irq(&ep->lock);

	/* kick interrupt, in case we are not called from the tasklet */
	if (req->to_host || was_empty)
		usb_gadget_handle_interrupts(0);

	return 0;
}

static int usbf_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct f_endpoint *ep = container_of(_ep, struct f_endpoint, ep);
	struct f_req *req = container_of(_req, struct f_req, req);
	struct f_drv *chip = ep->chip;

	/* dequeue the request */
	spin_lock_irq(&ep->lock);
	list_del_init(&req->queue);
	spin_unlock_irq(&ep->lock);

	TRACEEP(ep, "%s[%d][%3d] size %d\n", __func__,
			ep->id, req->seq, req->req.actual);

	/* don't modify queue heads during completion callback */
	if (chip->gadget.speed == USB_SPEED_UNKNOWN)
		req->req.status = -ESHUTDOWN;

	if (req->req.complete)
		usb_gadget_giveback_request(&ep->ep, &req->req);

	return 0;
}

/*
 * This function is called repeatedly on each endpoint. Its job is to
 * 'process' the current queued request (top of the queue) and 'complete'
 * it when it's finished.
 */
int usbf_ep_process_queue(struct f_endpoint *ep)
{
	struct f_req *req;

	spin_lock_irq(&ep->lock);
	req = list_first_entry_or_null(&ep->queue, struct f_req, queue);
	spin_unlock_irq(&ep->lock);

	if (req && req->process(ep, req) == 0) {
		/* 'complete' this request */
		usbf_ep_dequeue(&ep->ep, &req->req);

		/* If there are further requests, reschedule the tasklet */
		spin_lock_irq(&ep->lock);
		req = list_first_entry_or_null(&ep->queue, struct f_req, queue);
		spin_unlock_irq(&ep->lock);
		if (req)
			return 1;
	}

	return 0;
}

static int usbf_ep_halt(struct usb_ep *_ep, int halt)
{
	struct f_endpoint *ep = container_of(_ep, struct f_endpoint, ep);
	int ret = 0;

	TRACEEP(ep, "%s[%d] halt=%d\n", __func__, ep->id, halt);
	if (ep->drv->halt)
		ret = ep->drv->halt(ep, halt);
	return ret;
}

static void usbf_ep_reset(
	struct f_endpoint *ep)
{
	TRACEEP(ep, "%s[%d] reset\n", __func__, ep->id);
	if (ep->drv->reset)
		ep->drv->reset(ep);
	ep->status = 0;
	/* flush anything that was pending */
	while (!list_empty(&ep->queue)) {
		struct f_req *req = list_first_entry(&ep->queue,
					struct f_req, queue);
		TRACEEP(ep, "%s[%d][%3d] dequeueing\n", __func__,
			ep->id, req->seq);
		req->req.status = -ECONNRESET;
		usbf_ep_dequeue(&ep->ep, &req->req);
	}
}

static struct usb_ep_ops usbf_ep_ops = {
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

static void usbf_tasklet_process_irq(struct f_drv *chip, uint32_t int_status)
{
	int i;

	if (int_status & D_USB_USB_RST_INT) {
		if (chip->gadget.speed != USB_SPEED_UNKNOWN) {
			TRACE("%s disconnecting\n", __func__);
			chip->gadget.speed = USB_SPEED_UNKNOWN;
			if (chip->driver)
				chip->driver->disconnect(&chip->gadget);
		}
	}
	if (int_status & D_USB_SPEED_MODE_INT) {
		if (readl(&chip->regs->status) & D_USB_SPEED_MODE)
			chip->gadget.speed = USB_SPEED_HIGH;
		else
			chip->gadget.speed = USB_SPEED_FULL;
		TRACE("**** %s speed change: %s\n", __func__,
			chip->gadget.speed == USB_SPEED_HIGH ? "High" : "Full");
	}
#if 0
	if (int_status & D_USB_SPND_INT)
		TRACE("%s suspend clear\n", __func__);
	if (int_status & D_USB_RSUM_INT)
		TRACE("%s resume\n", __func__);
#endif
	for (i = 0; i < CFG_NUM_ENDPOINTS; i++) {
		struct f_endpoint *ep = &chip->ep[i];

		if (ep->disabled)
			continue;

		if (int_status & D_USB_USB_RST_INT)
			usbf_ep_reset(ep);
		/* speed change notification for endpoints */
		if ((int_status & D_USB_SPEED_MODE_INT) &&
				ep->drv->set_maxpacket)
			ep->drv->set_maxpacket(ep);

		/* Interrupt notification */
		if ((int_status & (D_USB_EP0_INT << i)) && ep->drv->interrupt)
			ep->drv->interrupt(ep);
	}
}

static void usbf_tasklet(unsigned long data, uint32_t int_status)
{
	struct f_drv *chip = (struct f_drv *)data;
	int i, busy = 1;

	usbf_tasklet_process_irq(chip, int_status);

	while (busy) {
		busy = 0;

		for (i = 0; i < CFG_NUM_ENDPOINTS; i++) {
			struct f_endpoint *ep = &chip->ep[i];

			if (!ep->disabled)
				busy |= usbf_ep_process_queue(ep);
		}
	}
}

static void usbf_attach(struct f_drv *chip)
{
	struct f_regs *regs = chip->regs;
	uint32_t ctrl = readl(&regs->control);

	/* Enable USB signal to Function PHY */
	ctrl &= ~D_USB_CONNECTB;
	/* D+ signal Pull-up */
	ctrl |=  D_USB_PUE2;
	/* Enable endpoint 0 */
	ctrl |=  D_USB_DEFAULT;

	writel(ctrl, &regs->control);
}

static void usbf_detach(struct f_drv *chip)
{
	struct f_regs *regs = chip->regs;
	uint32_t ctrl = readl(&regs->control);

	/* Disable USB signal to Function PHY */
	ctrl |=  D_USB_CONNECTB;
	/* Do not Pull-up D+ signal */
	ctrl &= ~D_USB_PUE2;
	/* Disable endpoint 0 */
	ctrl &= ~D_USB_DEFAULT;
	/* Disable the other endpoints */
	ctrl &= ~D_USB_CONF;

	writel(ctrl, &regs->control);

	writel(0, &regs->ep0.status);
}

static irqreturn_t usbf_irq(int irq, void *_chip)
{
	struct f_drv *chip = (struct f_drv *)_chip;
	struct f_regs *regs = chip->regs;
	uint32_t sysbint = readl(&regs->sysbint);

	/* clear interrupts */
	writel(sysbint, &regs->sysbint);
	if ((sysbint & D_SYS_VBUS_INT) == D_SYS_VBUS_INT) {
		/* Interrupt factor clear */
		if (readl(&regs->epctr) & D_SYS_VBUS_LEVEL) {
			TRACE("%s plugged in\n", __func__);
			usbf_attach(chip);
			usb_gadget_set_state(&chip->gadget, USB_STATE_POWERED);
		} else {
			TRACE("%s plugged out\n", __func__);
			usbf_detach(chip);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t usbf_epc_irq(int irq, void *_chip)
{
	struct f_drv *chip = (struct f_drv *)_chip;
	struct f_regs *regs = chip->regs;
	int int_status;
	int int_ep;
	int i;
	u32 int_en;
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);

	/* Disable interrupts */
	int_en = readl(&regs->int_enable);
	writel(0, &regs->int_enable);

	/*
	 * WARNING: Don't use the EP interrupt bits from this status reg as they
	 * won't be coherent with the EP status registers.
	 */
	int_status = readl(&regs->int_status) & 0xf;
	writel(~int_status, &regs->int_status);

	for (i = 0; i < CFG_NUM_ENDPOINTS; i++) {
		if (i == 0) {
			int_ep = readl(&regs->ep0.status) & readl(&regs->ep0.int_enable);
			writel(~int_ep, &regs->ep0.status);
		} else {
			int_ep = readl(&regs->ep[i - 1].status) & readl(&regs->ep[i - 1].int_enable);
			writel(~int_ep, &regs->ep[i - 1].status);
		}
		chip->ep[i].status = int_ep;
		/* Fake the global EP interrupt bits based on what we read */
		if (int_ep)
			int_status |= (D_USB_EP0_INT << i);
	}

	/* Process the interrupts */
	usbf_tasklet((unsigned long)chip, int_status);

	/* Enable interrupts */
	writel(int_en, &regs->int_enable);

	spin_unlock_irqrestore(&chip->lock, flags);

	return IRQ_HANDLED;
}

int usb_gadget_handle_interrupts(int index)
{
	usbf_irq(index, &controller);
	usbf_epc_irq(index, &controller);

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

	/* get out of reset */
	writel(readl(&chip->regs->epctr) & ~D_SYS_EPC_RST, &chip->regs->epctr);
	writel(readl(&chip->regs->epctr) & ~D_SYS_PLL_RST, &chip->regs->epctr);

	for (i = 0; i < CFG_NUM_ENDPOINTS; ++i) {
		struct f_endpoint init = {
			.ep = {
				.name = ep_name[i],
				.ops = &usbf_ep_ops,
				.maxpacket = CFG_EPX_MAX_PACKET_SIZE,
			},
			.id = i,
			.disabled = 1,
			.chip = chip,
		};
		struct f_endpoint *ep = chip->ep + i;

		if (!(readl(&chip->regs->usbssconf) & (1 << (16 + i)))) {
			TRACE("%s endpoint %d is not available\n", __func__, i);
			continue;
		}
		*ep = init;

		usb_ep_set_maxpacket_limit(&ep->ep, (unsigned short) ~0);

		INIT_LIST_HEAD(&ep->queue);
		spin_lock_init(&ep->lock);

		if (ep->id == 0) {
			usbf_ep0_init(ep);
		} else {
#ifdef CONFIG_USBF_RENESAS_FULL
			usbf_epn_init(ep);
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

	writel(readl(&chip->regs->sysmctr) | D_SYS_WBURST_TYPE, &chip->regs->sysmctr);

	writel(D_USB_INT_SEL | D_USB_SOF_RCV | D_USB_SOF_CLK_MODE, &chip->regs->control);

	/* Enable reset and mode change interrupts */
	writel(D_USB_USB_RST_EN | D_USB_SPEED_MODE_EN, &chip->regs->int_enable);
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
	/* get back into reset, but don't touch the USB PLL reset */
	writel(readl(&chip->regs->epctr) | D_SYS_EPC_RST, &chip->regs->epctr);

	return 0;
}
