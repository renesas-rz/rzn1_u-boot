/*
 * Copyright (C) 2017 Renesas Electronics Europe Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

/*
 * RZ/N1 GPIO controller driver. This is an old fashion driver style not
 * compliant with new driver model.
 * GPIO controller is Synopsys IP configured with 2 ports, A & B. Each port has
 * up to 32 pins associated with it.
 * RZN1 has 3 GPIO blocks, 1 to 3.
 *
 * Port A GPIOs, gpio global number is: ((GPIO block nr - 1) * 64) + pin
 * Port B GPIOs, gpio global number is: ((GPIO block nr - 1) * 64) + 32 + pin
 * e.g. global gpio number for gpio3b[26] = (2 * 64) + 32 + 26 = 186
 */

#include <common.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include "renesas/rzn1-memory-map.h"

/* verbose mode 1 otherwise 0*/
#define RZN1_GPIO_DEBUG 0

enum gpio_direction {
	GPIO_DIRECTION_OUT = 0,
	GPIO_DIRECTION_IN = 1,
};

#define GPIO_SWPORTA_DR		0x00
#define GPIO_SWPORTA_DDR	0x04
#define GPIO_SWPORTB_DR		0x0C
#define GPIO_SWPORTB_DDR	0x10
#define GPIO_INTEN		0x30
#define GPIO_INTMASK		0x34
#define GPIO_INTTYPE_LEVEL	0x38
#define GPIO_INT_POLARITY	0x3c
#define GPIO_INTSTATUS		0x40
#define GPIO_PORTA_DEBOUNCE	0x48
#define GPIO_PORTA_EOI		0x4c
#define GPIO_EXT_PORTA		0x50
#define GPIO_EXT_PORTB		0x54

/* Base address of GPIO controller */
static const u32 gpio_base[3] = {
	RZN1_GPIO0_BASE,
	RZN1_GPIO1_BASE,
	RZN1_GPIO2_BASE
};

#define RZN1_GPIO(block, port, x) \
	(((block) - 1) * 64) + (((port) - 'a') * 32) + (x)

static const u8 gpio_nr[] = {
	RZN1_GPIO(1, 'a', 0), RZN1_GPIO(1, 'b', 0), RZN1_GPIO(1, 'b', 1),
	RZN1_GPIO(1, 'a', 1), RZN1_GPIO(1, 'a', 2), RZN1_GPIO(1, 'b', 2),
	RZN1_GPIO(1, 'b', 3), RZN1_GPIO(1, 'b', 4), RZN1_GPIO(1, 'b', 5),
	RZN1_GPIO(1, 'a', 3), RZN1_GPIO(1, 'a', 4), RZN1_GPIO(1, 'b', 6),
	RZN1_GPIO(1, 'a', 5), RZN1_GPIO(1, 'b', 7), RZN1_GPIO(1, 'b', 8),
	RZN1_GPIO(1, 'a', 6), RZN1_GPIO(1, 'a', 7), RZN1_GPIO(1, 'b', 9),
	RZN1_GPIO(1, 'b', 10), RZN1_GPIO(1, 'b', 11), RZN1_GPIO(1, 'b', 12),
	RZN1_GPIO(1, 'a', 8), RZN1_GPIO(1, 'a', 9), RZN1_GPIO(1, 'b', 13),
	RZN1_GPIO(1, 'a', 10), RZN1_GPIO(1, 'b', 14), RZN1_GPIO(1, 'b', 15),
	RZN1_GPIO(1, 'a', 11), RZN1_GPIO(1, 'a', 12), RZN1_GPIO(1, 'b', 16),
	RZN1_GPIO(1, 'b', 17), RZN1_GPIO(1, 'b', 18), RZN1_GPIO(1, 'b', 19),
	RZN1_GPIO(1, 'a', 13), RZN1_GPIO(1, 'a', 14), RZN1_GPIO(1, 'b', 20),
	RZN1_GPIO(1, 'a', 15), RZN1_GPIO(1, 'b', 21), RZN1_GPIO(1, 'b', 22),
	RZN1_GPIO(1, 'a', 16), RZN1_GPIO(1, 'a', 17), RZN1_GPIO(1, 'b', 23),
	RZN1_GPIO(1, 'b', 24), RZN1_GPIO(1, 'b', 25), RZN1_GPIO(1, 'b', 26),
	RZN1_GPIO(1, 'a', 18), RZN1_GPIO(1, 'a', 19), RZN1_GPIO(1, 'b', 27),
	RZN1_GPIO(1, 'a', 20), RZN1_GPIO(1, 'b', 28), RZN1_GPIO(1, 'b', 29),
	RZN1_GPIO(1, 'a', 21), RZN1_GPIO(1, 'a', 22), RZN1_GPIO(1, 'b', 30),
	RZN1_GPIO(1, 'b', 31), RZN1_GPIO(2, 'b', 0), RZN1_GPIO(2, 'b',  1),
	RZN1_GPIO(1, 'a', 23), RZN1_GPIO(1, 'a', 24), RZN1_GPIO(2, 'b', 2),
	RZN1_GPIO(2, 'b', 3), RZN1_GPIO(2, 'b', 4), RZN1_GPIO(1, 'a', 25),
	RZN1_GPIO(1, 'a', 26), RZN1_GPIO(1, 'a', 27), RZN1_GPIO(1, 'a', 28),
	RZN1_GPIO(1, 'a', 29), RZN1_GPIO(1, 'a', 30), RZN1_GPIO(1, 'a', 31),
	RZN1_GPIO(2, 'a', 0), RZN1_GPIO(2, 'a', 1), RZN1_GPIO(2, 'a', 2),
	RZN1_GPIO(2, 'a', 3), RZN1_GPIO(2, 'a', 4), RZN1_GPIO(2, 'b', 5),
	RZN1_GPIO(2, 'b', 6), RZN1_GPIO(2, 'b', 7), RZN1_GPIO(2, 'b', 8),
	RZN1_GPIO(2, 'b', 9), RZN1_GPIO(2, 'b', 10), RZN1_GPIO(2, 'b',  11),
	RZN1_GPIO(2, 'b', 12), RZN1_GPIO(2, 'b', 13), RZN1_GPIO(2, 'b', 14),
	RZN1_GPIO(2, 'b', 15), RZN1_GPIO(2, 'b', 16), RZN1_GPIO(2, 'b', 17),
	RZN1_GPIO(2, 'b', 18), RZN1_GPIO(2, 'b', 19), RZN1_GPIO(2, 'b', 20),
	RZN1_GPIO(2, 'b', 21), RZN1_GPIO(2, 'b', 22), RZN1_GPIO(2, 'b', 23),
	RZN1_GPIO(2, 'b', 24), RZN1_GPIO(2, 'b', 25), RZN1_GPIO(2, 'a', 5),
	RZN1_GPIO(2, 'a', 6), RZN1_GPIO(2, 'a', 7), RZN1_GPIO(2, 'a', 8),
	RZN1_GPIO(2, 'a', 9), RZN1_GPIO(2, 'a', 10), RZN1_GPIO(2, 'a',  11),
	RZN1_GPIO(2, 'a', 12), RZN1_GPIO(2, 'a', 13), RZN1_GPIO(2, 'a', 14),
	RZN1_GPIO(2, 'a', 15), RZN1_GPIO(2, 'a', 16), RZN1_GPIO(2, 'a', 17),
	RZN1_GPIO(2, 'a', 18), RZN1_GPIO(2, 'a', 19), RZN1_GPIO(2, 'a', 20),
	RZN1_GPIO(2, 'a', 21), RZN1_GPIO(2, 'a', 22), RZN1_GPIO(2, 'a', 23),
	RZN1_GPIO(2, 'a', 24), RZN1_GPIO(2, 'a', 25), RZN1_GPIO(2, 'a', 26),
	RZN1_GPIO(2, 'a', 27), RZN1_GPIO(2, 'a', 28), RZN1_GPIO(2, 'a', 29),
	RZN1_GPIO(2, 'a', 30), RZN1_GPIO(2, 'a', 31), RZN1_GPIO(3, 'a', 0),
	RZN1_GPIO(3, 'a', 1), RZN1_GPIO(3, 'a', 2), RZN1_GPIO(3, 'a', 3),
	RZN1_GPIO(3, 'a', 4), RZN1_GPIO(3, 'a', 5), RZN1_GPIO(3, 'a', 6),
	RZN1_GPIO(3, 'a', 7), RZN1_GPIO(3, 'a', 8), RZN1_GPIO(3, 'a', 9),
	RZN1_GPIO(3, 'a', 10), RZN1_GPIO(3, 'a', 11), RZN1_GPIO(3, 'a', 12),
	RZN1_GPIO(3, 'a', 13), RZN1_GPIO(3, 'a', 14), RZN1_GPIO(3, 'a', 15),
	RZN1_GPIO(3, 'a', 16), RZN1_GPIO(3, 'a', 17), RZN1_GPIO(3, 'a', 18),
	RZN1_GPIO(3, 'a', 19), RZN1_GPIO(3, 'a', 20), RZN1_GPIO(3, 'a', 21),
	RZN1_GPIO(3, 'a', 22), RZN1_GPIO(3, 'a', 23), RZN1_GPIO(3, 'a', 24),
	RZN1_GPIO(3, 'a', 25), RZN1_GPIO(3, 'a', 26), RZN1_GPIO(3, 'a', 27),
	RZN1_GPIO(2, 'b', 26), RZN1_GPIO(2, 'b', 27), RZN1_GPIO(2, 'b', 28),
	RZN1_GPIO(2, 'b', 29), RZN1_GPIO(2, 'b', 30), RZN1_GPIO(2, 'b', 31),
	RZN1_GPIO(3, 'a', 28), RZN1_GPIO(3, 'a', 29), RZN1_GPIO(3, 'a', 30),
	RZN1_GPIO(3, 'a', 31), RZN1_GPIO(3, 'b', 0), RZN1_GPIO(3, 'b',  1),
	RZN1_GPIO(3, 'b', 2), RZN1_GPIO(3, 'b', 3), RZN1_GPIO(3, 'b', 4),
	RZN1_GPIO(3, 'b', 5), RZN1_GPIO(3, 'b', 6), RZN1_GPIO(3, 'b', 7),
	RZN1_GPIO(3, 'b', 8), RZN1_GPIO(3, 'b', 9),
};

static int gpio_get_num_port_pin(unsigned gpio, int *num, char *port, int *pin)
{
	int n;
	char po;
	int pi;

	if (gpio >= 64 * 3)
		return -1;

	n = gpio / 64;

	if ((gpio - n * 64) < 32) {
		po = 'a';
		pi = gpio - n * 64;
	} else {
		po = 'b';
		pi = gpio - n * 64 - 32;
	}

	if (num)
		*num = n;
	if (port)
		*port = po;
	if (pin)
		*pin = pi;

	return 0;
}

static int gpio_set_direction(unsigned pin, enum gpio_direction direction)
{
	unsigned gpio = gpio_nr[pin];
	int gpio_num;
	char gpio_port;
	int gpio_pin;
	u32 reg;

	if (gpio_get_num_port_pin(gpio, &gpio_num, &gpio_port, &gpio_pin) == -1)
		return -1;

	if (gpio_port == 'a')
		reg = gpio_base[gpio_num] + GPIO_SWPORTA_DDR;
	else
		reg = gpio_base[gpio_num] + GPIO_SWPORTB_DDR;

	if (direction == GPIO_DIRECTION_OUT)
		setbits_le32(reg, 1 << gpio_pin);
	else
		clrbits_le32(reg, 1 << gpio_pin);

	return 0;
}

int nondm_gpio_set_value(unsigned pin, int value)
{
	unsigned gpio = gpio_nr[pin];
	int gpio_num;
	char gpio_port;
	int gpio_pin;
	u32 reg;

	if (gpio_get_num_port_pin(gpio, &gpio_num, &gpio_port, &gpio_pin) == -1)
		return -1;

	if (gpio_port == 'a')
		reg = gpio_base[gpio_num] + GPIO_SWPORTA_DR;
	else
		reg = gpio_base[gpio_num] + GPIO_SWPORTB_DR;

	if (value)
		setbits_le32(reg, 1 << gpio_pin);
	else
		clrbits_le32(reg, 1 << gpio_pin);

	return 0;
}

int nondm_gpio_get_value(unsigned pin)
{
	unsigned gpio = gpio_nr[pin];
	int gpio_num;
	char gpio_port;
	int gpio_pin;
	u32 reg;

	if (gpio_get_num_port_pin(gpio, &gpio_num, &gpio_port, &gpio_pin) == -1)
		return -1;

	if (gpio_port == 'a')
		reg = gpio_base[gpio_num] + GPIO_EXT_PORTA;
	else
		reg = gpio_base[gpio_num] + GPIO_EXT_PORTB;

	return !!(readl(reg) & (1 << gpio_pin));
}

int nondm_gpio_request(unsigned pin, const char *label)
{
	unsigned gpio = gpio_nr[pin];
	int gpio_num;
	char gpio_port;
	int gpio_pin;
	int ret;

	ret = gpio_get_num_port_pin(gpio, &gpio_num, &gpio_port, &gpio_pin);

	if (ret == -1)
		debug_cond(RZN1_GPIO_DEBUG, "%s gpio_%d failed\n", __func__, gpio);
	else
		debug_cond(RZN1_GPIO_DEBUG,"%s gpio_%d = GPIO_%d, port_%c, pin_%d\n",
			__func__, gpio, gpio_num, gpio_port, gpio_pin);

	return ret;
}

int nondm_gpio_free(unsigned gpio)
{
	return 0;
}

int nondm_gpio_direction_input(unsigned gpio)
{
	return gpio_set_direction(gpio, GPIO_DIRECTION_IN);
}

int nondm_gpio_direction_output(unsigned gpio, int value)
{
	int ret = gpio_set_direction(gpio, GPIO_DIRECTION_OUT);

	if (ret < 0)
		return ret;

	nondm_gpio_set_value(gpio, value);

	return 0;
}
