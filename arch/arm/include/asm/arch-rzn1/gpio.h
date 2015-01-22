/*
* Copyright (C) 2016 Renesas Electronics Europe Ltd
*
* SPDX-License-Identifier:	GPL-2.0+
*/

#ifndef _RZN1_GPIO_H_
#define _RZN1_GPIO_H_

#include <asm-generic/gpio.h>

/* Helper function to convert pin number to gpio number */
u8 rzn1_pin_to_gpio(u8 pin);

#endif /* _RZN1_GPIO_H_ */
